fix=$(CURDIR)/fix
all:
	tar jxf /usr/src/linux-source-4.8.0.tar.bz2
	(									\
		cd linux-source-4.8.0 &&					\
		make zfcpdump_defconfig && 					\
		$(fix) &&							\
		make bzImage &&							\
		cp arch/s390/boot/bzImage ../zfcpdump_part.image;		\
	)
