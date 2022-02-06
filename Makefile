
default: qr

qr: main
	cat main | qrencode -8 -o qr.png

main: main.c
	clang -s -Os \
		-nostdlib \
		-ffreestanding \
		-fno-stack-protector \
		-fno-unwind-tables \
		-fno-asynchronous-unwind-tables \
		-fomit-frame-pointer  \
		-ffunction-sections \
		-fdata-sections \
		-Wl,--gc-sections \
		-static \
		-z noseparate-code \
		-o main main.c