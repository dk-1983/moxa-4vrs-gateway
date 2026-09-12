#ifndef FOURVRS_PLATFORM_H
#define FOURVRS_PLATFORM_H
#ifdef FOURVRS_LINUX24
#define FOURVRS_NETWORK_STREAM_IPC 1
#define FOURVRS_LAN_PREFIX "ixp"
#define FOURVRS_OPTIONAL_VENDOR_INTERFACE "eth0"
#define FOURVRS_IFDOWN_FORCE "--force"
#define FOURVRS_APACHE_GATE "etc/rc.d/rc3.d/S21apache"
#define FOURVRS_APACHE_BINARY "/usr/sbin/httpd"
#define FOURVRS_APACHE_ENTRY "../../usr/bin/apachectl"
#define FOURVRS_ELF_FLAGS 0x202U
#define FOURVRS_ELF_LOADER "/lib/ld-linux.so.2"
#define FOURVRS_MODEL_SHORT "Moxa UC-7420-LX"
#define FOURVRS_MODEL_FULL "Moxa UC-7420-LX"
#else
#define FOURVRS_LAN_PREFIX "eth"
#define FOURVRS_OPTIONAL_VENDOR_INTERFACE "eth2"
#define FOURVRS_IFDOWN_FORCE "-f"
#define FOURVRS_APACHE_GATE "etc/rc.d/rcS.d/S21apache"
#define FOURVRS_APACHE_BINARY "/bin/httpd"
#define FOURVRS_APACHE_ENTRY "../../usr/sbin/apachectl"
#define FOURVRS_ELF_FLAGS 0x04000002U
#define FOURVRS_ELF_LOADER "/lib/ld-linux.so.3"
#define FOURVRS_MODEL_SHORT "Moxa UC-7420 Plus"
#define FOURVRS_MODEL_FULL "Moxa UC-7420-LX Plus"
#endif
#endif
