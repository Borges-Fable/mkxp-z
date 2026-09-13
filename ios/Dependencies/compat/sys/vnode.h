/* The iOS SDK does not ship <sys/vnode.h>. Ruby's dir.c only needs the
 * vnode type and tag values that getattrlist(2) reports in ATTR_CMN_OBJTYPE
 * and ATTR_CMN_OBJTAG; both enums match xnu's bsd/sys/vnode.h. */
#ifndef MKXPZ_IOS_SYS_VNODE_H
#define MKXPZ_IOS_SYS_VNODE_H
enum vtype { VNON, VREG, VDIR, VBLK, VCHR, VLNK, VSOCK, VFIFO, VBAD, VSTR, VCPLX };
enum vtagtype {
    VT_NON, VT_UFS, VT_NFS, VT_MFS, VT_MSDOSFS, VT_LFS, VT_LOFS, VT_FDESC,
    VT_PORTAL, VT_NULL, VT_UMAP, VT_KERNFS, VT_PROCFS, VT_AFS, VT_ISOFS,
    VT_MOCKFS, VT_HFS, VT_ZFS, VT_DEVFS, VT_WEBDAV, VT_UDF, VT_AFP,
    VT_CDDA, VT_CIFS, VT_OTHER
};
#endif
