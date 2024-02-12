# File delivery plugin

This plugin provides file delivery (as an addition to the builtin SMTP/LMTP) to a configurable folder.
The plugin leverages the [queue delivery plugin API](https://docs.halon.io/manual/plugins_native.html#id3).
Max connection concurrency and rates are configured as usual, using [active queue pickup policies](https://docs.halon.io/manual/queue.html#queue-pickup-policies).
Messages are written in RFC-822 format (.eml).

## Installation

Follow the [instructions](https://docs.halon.io/manual/comp_install.html#installation) in our manual to add our package repository and then run the below command.

### Ubuntu

```
apt-get install halon-extras-file-deliver
```

### RHEL

```
yum install halon-extras-file-deliver
```

## Configuration

### smtpd.yaml

```
plugins:
  - id: file-deliver
    config:
      fsync: true
      path: /var/halon/spool/output
      tmppath: /var/halon/spool/tmp
      threads: 4
```

### smtpd-app.yaml

Add a "placeholder" transport, with connection destination 0.0.0.0 (this plugin will leverage retry delays and other queue features)

```
transportgroups:
  - id: file-deliver-group
    transports:
      - id: file-deliver
        connection:
          server: 0.0.0.0
```

### Pre-delivery script hook

Plugin options are:

* fsync (boolean) default true
* filename (string) default is `transactionid_queueid.eml` (sub-folders are supported)

```
Try([
    "plugin" => [
        "id" => "file-deliver",
        "arguments" => [
            "filename" => "foo.eml",
            "fsync" => true,
        ]
    ]
]);
```

### Post-delivery script hook

In the post-delivery script hook there are two ways this plugin may return a result. All errors are considered temporary.

```
$arguments["attempt"]["error"]["message"] = "The error message...";
$arguments["attempt"]["error"]["temporary"] = true;
```

or 

```
$arguments["attempt"]["result"]["code"] = 250;
$arguments["attempt"]["result"]["reason"] = ["FILE"];
```
