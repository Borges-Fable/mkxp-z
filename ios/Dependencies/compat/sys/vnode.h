/* The iOS SDK does not ship <sys/vnode.h>. Ruby's dir.c only needs the
 * vnode type values that getattrlist(2) reports in ATTR_CMN_OBJTYPE;
 * these match the macOS header. */
#ifndef MKXPZ_IOS_SYS_VNODE_H
#define MKXPZ_IOS_SYS_VNODE_H
enum vtype { VNON, VREG, VDIR, VBLK, VCHR, VLNK, VSOCK, VFIFO, VBAD, VSTR, VCPLX };
#endif
