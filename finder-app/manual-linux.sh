#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/home/nunezbenj/sera/buildroot/embeded_arm64
OUTDIR=/tmp/aeld

KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
TOOLCHAIN_DIR=/home/nunezbenj/arm-cross-compiler


if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
	# Deep Clean the kernel source tree
	make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- mrproper
	# Configure the kernel for the target architecture
	make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig
	# Compile the kernel
	make -j4 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- all
	# Build the kernel modules
	make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- modules
	# Build the device tree blobs
	make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- dtbs
fi

echo "Adding the Image in outdir"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir -p "${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log
echo "Creating the base directories in the root filesystem ${OUTDIR}/rootfs  ... OK"

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
	# Clean the build environment
	make distclean
	# Create a default configuration
	make defconfig
    echo "Cloning busybox version ${BUSYBOX_VERSION} in ${OUTDIR}/busybox  ... OK"
else
    cd busybox
    echo "busybox ${BUSYBOX_VERSION} was already compiled in ${OUTDIR}/busybox  ... OK"
fi

# TODO: Make and install busybox
# Compile the source code with the specified architecture and cross compiler
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
# Install the compiled binaries into the specified root directory
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
echo "busybox ${BUSYBOX_VERSION} instalation ${OUTDIR}/busybox  ... OK"

# TODO: Add library dependencies to rootfs
cd "${OUTDIR}/rootfs"
echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"
echo "Copying necessary library dependencies to rootfs"

echo "TOOLCHAIN_DIR=${TOOLCHAIN_DIR}"
echo "OUTDIR=${OUTDIR}"
ls -l ${TOOLCHAIN_DIR}/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib/
ls -l ${TOOLCHAIN_DIR}/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib64/

# Copy the program interpreter
cp -L ${TOOLCHAIN_DIR}/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib/
# Copy shared libraries
cp -L ${TOOLCHAIN_DIR}/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib64/libm.so.6 ${OUTDIR}/rootfs/lib64/
cp -L ${TOOLCHAIN_DIR}/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib64/libresolv.so.2 ${OUTDIR}/rootfs/lib64/
cp -L ${TOOLCHAIN_DIR}/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib64/libc.so.6 ${OUTDIR}/rootfs/lib64/
echo "Library dependencies copied to ${OUTDIR}/rootfs/lib64/  ... OK"

# TODO: Make device nodes
sudo mknod -m 666 dev/null c 1 3
#sudo mknod -m 622 dev/console c 5 1
sudo mknod -m 666 dev/console c 5 1
echo "Device nodes null and console created in ${OUTDIR}/rootfs/dev/  ... OK"

# TODO: Clean and build the writer utility
# bnunez: Took me hours and hours to figure out two below lines need to be all the way at the end ... not sure what needs to be done in this TODO
# find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
# echo "Bundle root file system ${OUTDIR}/rootfs into file ${OUTDIR}/initramfs.cpio  ... OK"

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs The -L option tells cp to follow symbolic link
cp -rL ${FINDER_APP_DIR}/* ${OUTDIR}/rootfs/home/
 
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}
echo "Copy assignemt3 scripts to ${OUTDIR}/rootfs/home/ and Image to ${OUTDIR}  ... OK"

# TODO: Chown the root directory
sudo chown -R root:root ${OUTDIR}/rootfs
echo "Be sure root is the owner of all files in ${OUTDIR}/rootfs  ... OK"

# TODO: Create initramfs.cpio.gz
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
echo "Bundle root file system ${OUTDIR}/rootfs into file ${OUTDIR}/initramfs.cpio  ... OK"
cd ${OUTDIR}
gzip -f initramfs.cpio
echo "InitRamFileSystem file created ${OUTDIR}/initramfs.cpio.gz  ... OK"

