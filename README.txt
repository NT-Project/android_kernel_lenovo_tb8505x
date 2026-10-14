1.Compile host system：ubuntu 18.04
The software that needs to be installed to build the kernel：
sudo apt-get install git ccache automake flex lzop bison \
gperf build-essential zip curl zlib1g-dev zlib1g-dev:i386 \
g++-multilib python-networkx libxml2-utils bzip2 libbz2-dev \
libbz2-1.0 libghc-bzlib-dev squashfs-tools pngcrush \
schedtool dpkg-dev liblz4-tool make optipng maven libssl-dev \
pwgen libswitch-perl policycoreutils minicom libxml-sax-base-perl \
libxml-simple-perl bc libc6-dev-i386 lib32ncurses5-dev \
x11proto-core-dev libx11-dev lib32z-dev libgl1-mesa-dev xsltproc unzip

2.Compile using Kernel_build.sh：
./Kernel_build.sh

3.Product Description：
o@o:~/work/kernel-4.9$ ll ./out_dir/arch/arm64/boot
drwxrwxr-x  3 o o     4096 11月 26 17:47 ./
drwxrwxr-x  9 o o     4096 11月 26 17:44 ../
drwxrwxr-x 25 o o     4096 11月 26 17:45 dts/
-rw-rw-r--  1 o o 27684872 11月 26 17:47 Image
-rw-rw-r--  1 o o      210 11月 26 17:47 .Image.cmd
-rw-rw-r--  1 o o 10137089 11月 26 17:47 Image.gz
-rw-rw-r--  1 o o      147 11月 26 17:47 .Image.gz.cmd
-rw-rw-r--  1 o o 10236323 11月 26 17:47 Image.gz-dtb
-rw-rw-r--  1 o o      185 11月 26 17:47 .Image.gz-dtb.cmd
-rw-rw-r--  1 o o    99234 11月 26 17:47 mtk.dtb
-rw-rw-r--  1 o o      145 11月 26 17:47 .mtk.dtb.cmd

4.build log 
You can view the current compilation path:
Analyze and compile any issues or provide feedback encountered during the Build. log. txt process.

F&Q:

bash: ./Kernel_build.sh: insufficient privilege
: chmod +x Kernel_build.sh  or  chmod 777 Kernel_build.sh


/usr/bin/ld: scripts/dtc/dtc-parser.tab.o:(.bss+0x10): multiple definition of `yylloc'; scripts/dtc/dtc-lexer.lex.o:(.bss+0x0): first defined here
collect2: error: ld returned 1 exit status
:Use GCC-9 or earlier versions
sudo apt-get install gcc-9
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 50 --slave /usr/bin/g++ g++ /usr/bin/g++-9
