#include <HalonMTA.h>
#include <string>
#include <thread>
#include <queue>
#include <list>
#include <mutex>
#include <condition_variable>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <mkdirp.hpp>

struct halon {
	HalonDeliverContext *hdc;
	FILE *fp = nullptr;
	int outfd = -1;
	bool fsync;
	std::string filename;
	std::string filename2;
};

std::list<std::thread> threads;
bool cquit = false;
std::mutex mcopy;
std::condition_variable cvcopy;
std::queue<halon*> qcopy;

struct
{
	bool fsync = true;
	std::string basedir;
	std::string tmpbasedir;
} globalSettings;

void fcopy()
{
	pthread_setname_np(pthread_self(), "p/file/copy");
	char buf[8192];
	while (true)
	{
		std::unique_lock<std::mutex> ul(mcopy);
		cvcopy.wait(ul, [&]() { return cquit || !qcopy.empty(); });
		if (cquit)
			break;
		auto a = qcopy.front();
		qcopy.pop();
		ul.unlock();

		while (true)
		{
			size_t r = fread(buf, 1, sizeof(buf), a->fp);
			if (r == 0)
				break;
			if (write(a->outfd, buf, r) != (ssize_t)r)
			{
				HalonMTA_deliver_setinfo(a->hdc, HALONMTA_ERROR_REASON,
						("Could not write (" + a->filename + "): " + std::string(strerror(errno))).c_str(), 0);
				unlink(a->filename.c_str());
				goto done;
			}
		}

		if (a->fsync && fsync(a->outfd) != 0)
		{
			HalonMTA_deliver_setinfo(a->hdc, HALONMTA_ERROR_REASON,
					("Could not fsync (" + a->filename + "): " + std::string(strerror(errno))).c_str(), 0);
			unlink(a->filename.c_str());
			goto done;
		}

		if (rename(a->filename.c_str(), a->filename2.c_str()) != 0)
		{
			if (errno == ENOENT)
			{
				auto slash = a->filename2.find_last_of('/');
				if (slash == std::string::npos)
				{
					errno = EINVAL;
					goto bad;
				}
				if (!mkdirp(a->filename2.substr(0, slash), 0777))
					goto bad;
				if (rename(a->filename.c_str(), a->filename2.c_str()) == 0)
					goto good;
			}
bad:
			HalonMTA_deliver_setinfo(a->hdc, HALONMTA_ERROR_REASON,
					("Could not rename (" + a->filename + ", " + a->filename2 + "): " + std::string(strerror(errno))).c_str(), 0);
			unlink(a->filename.c_str());
			goto done;
		}
good:
		HalonMTA_deliver_setinfo(a->hdc, HALONMTA_RESULT_REASON, "FILE", 0);
done:
		close(a->outfd);
		HalonMTA_deliver_done(a->hdc);
		delete a;
	}
}

HALON_EXPORT
int Halon_version()
{
	return HALONMTA_PLUGIN_VERSION;
}

HALON_EXPORT
bool Halon_init(HalonInitContext *hic)
{
	HalonConfig* cfg;
	HalonMTA_init_getinfo(hic, HALONMTA_INIT_CONFIG, nullptr, 0, &cfg, nullptr);

	const char* path_ = HalonMTA_config_string_get(HalonMTA_config_object_get(cfg, "path"), nullptr);
	if (path_)
		globalSettings.basedir = path_;

	const char* tmppath_ = HalonMTA_config_string_get(HalonMTA_config_object_get(cfg, "tmppath"), nullptr);
	if (tmppath_)
		globalSettings.tmpbasedir = tmppath_;
	else
		globalSettings.tmpbasedir = globalSettings.basedir;

	const char* fsync_ = HalonMTA_config_string_get(HalonMTA_config_object_get(cfg, "fsync"), nullptr);
	if (fsync_)
		globalSettings.fsync = std::string(fsync_) != "false";

	size_t threadcount = 1;
	const char* threads_ = HalonMTA_config_string_get(HalonMTA_config_object_get(cfg, "threads"), nullptr);
	if (threads_)
		threadcount = strtoul(threads_, nullptr, 10);

	for (size_t i = 0; i < threadcount; ++i)
		threads.push_back(std::thread(fcopy));
	return true;
}

HALON_EXPORT
void Halon_cleanup()
{
	cquit = true;
	cvcopy.notify_all();
	for (auto & t : threads)
		t.join();
}

HALON_EXPORT
void Halon_deliver(HalonDeliverContext *hdc)
{
	if (globalSettings.basedir.empty())
	{
		HalonMTA_deliver_setinfo(hdc, HALONMTA_ERROR_REASON, "Path not configured", 0);
		HalonMTA_deliver_done(hdc);
		return;
	}

	FILE *fp = nullptr;
	if (!HalonMTA_deliver_getinfo(hdc, HALONMTA_INFO_FILE, nullptr, 0, (void*)&fp, nullptr))
	{
		HalonMTA_deliver_setinfo(hdc, HALONMTA_ERROR_REASON, "No file (internal error)", 0);
		HalonMTA_deliver_done(hdc);
		return;
	}

	HalonQueueMessage* hqm;
	const char* transactionid;
	size_t queueid;
	if (!HalonMTA_deliver_getinfo(hdc, HALONMTA_INFO_MESSAGE, nullptr, 0, (void*)&hqm, nullptr) ||
		!HalonMTA_message_getinfo(hqm, HALONMTA_MESSAGE_TRANSACTIONID, nullptr, 0, &transactionid, nullptr) ||
		!HalonMTA_message_getinfo(hqm, HALONMTA_MESSAGE_QUEUEID, nullptr, 0, &queueid, nullptr))
	{
		HalonMTA_deliver_setinfo(hdc, HALONMTA_ERROR_REASON, "No message (internal error)", 0);
		HalonMTA_deliver_done(hdc);
		return;
	}

	bool fsync_ = globalSettings.fsync;
	const char *filename = nullptr;

	const HalonHSLValue *arguments = nullptr;
	if (HalonMTA_deliver_getinfo(hdc, HALONMTA_INFO_ARGUMENTS, nullptr, 0, &arguments, nullptr))
	{
		const HalonHSLValue *hv_fsync = HalonMTA_hsl_value_array_find(arguments, "fsync");
		if (hv_fsync && !HalonMTA_hsl_value_get(hv_fsync, HALONMTA_HSL_TYPE_BOOLEAN, &fsync_, nullptr))
		{
			HalonMTA_deliver_setinfo(hdc, HALONMTA_ERROR_REASON, "Bad fsync value", 0);
			HalonMTA_deliver_done(hdc);
			return;
		}

		const HalonHSLValue *hv_filename = HalonMTA_hsl_value_array_find(arguments, "filename");
		if (hv_filename && !HalonMTA_hsl_value_get(hv_filename, HALONMTA_HSL_TYPE_STRING, &filename, nullptr))
		{
			HalonMTA_deliver_setinfo(hdc, HALONMTA_ERROR_REASON, "Bad filename value", 0);
			HalonMTA_deliver_done(hdc);
			return;
		}
	}

	auto h = new halon;
	h->hdc = hdc;
	h->fp = fp;
	h->fsync = fsync_;
	h->filename = globalSettings.tmpbasedir + "/" + std::string(transactionid) + "_" + std::to_string(queueid) + ".tmp";
	if (filename)
		h->filename2 = globalSettings.basedir + "/" + filename;
	else
		h->filename2 = globalSettings.basedir + "/" + std::string(transactionid) + "_" + std::to_string(queueid) + ".eml";
	h->outfd = open(h->filename.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0666);

	if (h->outfd < 0)
	{
		HalonMTA_deliver_setinfo(hdc, HALONMTA_ERROR_REASON,
			("Could not open (" + h->filename + "): " + std::string(strerror(errno))).c_str(), 0);
		HalonMTA_deliver_done(hdc);
		delete h;
		return;
	}

	mcopy.lock();
	qcopy.push(h);
	mcopy.unlock();
	cvcopy.notify_one();
}