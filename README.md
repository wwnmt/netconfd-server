# netconfd 

============

netconfd is a NETCONF server and it enables device provisioning through
NETCONF protocol.

- extend freenetconfd https://github.com/freenetconf/freenetconfd.git
- written in C

### dependencies

Install *autoconf*, *cmake*, *git*, *json-c*, *libtool*, *pkg-config* and
*zlib* using your package manager while the following dependancies one will
likely need to install from source:

- [*libubox*](http://git.openwrt.org/?p=project/libubox.git;a=summary)
- [*uci*](http://nbd.name/gitweb.cgi?p=uci.git;a=summary)
- [*libroxml*](http://www.libroxml.net/)
- [*ubus*](http://wiki.openwrt.org/doc/techref/ubus)

### building netconfd

The build procedure itself is simple:

```
cd netconfd/build
cmake ..
make
```

### configuring netconfd

`/etc/config/netconfd`. The defaults can be copied from the source:

```
config netconfd
    option addr '127.0.0.1'
    option port '1831'
```

### running netconfd

```
./start.sh
```
