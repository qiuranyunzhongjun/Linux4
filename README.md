# 实验要求
## 基本要求

|函数名称|Linux对应脚本|说明|
|------|-----------|----|
|`fd_ls`|`ls`|显示当前目录下包含文件的信息|
|`fd_cd`|`cd`|从当前面目录切换到指定目录|
|`fd_df`|`rm`|删除指定文件|
|`fd_cf`|N/A|新建文件|

其中 `fd_cf` 命令在Linux里面没有直接对应函数。在Linux中，大部分写入文件的操作，只要文件不存在，就会新建文件。

## 提高要求

|函数名称|Linux对应脚本|说明|标注|
|-------|-----------|----|---|
|`mkdir`|`mkdir`|新建文件夹|**Step A**|
|`rmdir`|`rmdir`|删除文件夹|**Step B**|
|改进`cd`|`cd`|增加对于绝对路径，多级路径的支持|**Step C**|
|N/A|N/A|各功能可同图形界面中操作替换使用|**Step D**|

提高要求没有必要全部都做，量力而行做好即可。**Step N** 的意思后文会提到。实验的主要目的是理解FAT16这个文件系统结构，并可以对该文件系统进行操作。虽然该结构已经停止使用，但是理解这个结构对于未来理解FAT32，以及其他文件结构都有很大的帮助。

# 时间规划
原本计划13~15周完成文件系统实验，由于15周端午节假期的原因，我们适当降低该次实验难度，提前讲解实验，并于14周检查。

|时间|内容|
|---|---|
|12周|讲解实验基本要求|
|13周|详细讲解实验 & 解答|
|14周|总结 & 检查|

# 实验内容
文件系统实验是4个试验中最简单的，而且因为放假的原因，适当的降低了基本要求的难度。完成该实验的主要时间消耗为理解FAT16结构而不是写程序。参考资料：《操作系统实用教程实验指导》P85~119. 一份FAT16文件系统说明pdf，PPT，以及这份README。

## 实验步骤

### 准备工作

1.打开终端\(Terminal\)，`cd` 到想要存放工程的文件夹下，并执行如下命令将程序下载到本地。
```shell
git clone https://github.com/tonyshaw/Linux4.git
```

3.`cd` 到Linux4目录中，创建大小为32M的全0空文件，作为虚拟优盘。
```shell
dd if=/dev/zero of=data bs=32M count=1
```
其中`dd`会克隆给定的输入内容并写入到输出文件。`if`代表输入文件，`of`代表输出文件，`bs`代表以字节为单位的块大小，`count`表示要被复制的块数。`/dev/zero`是一个字符设备，他会不断返回0值字节

3.将虚拟优盘格式化为FAT16文件系统。
```shell
mkfs.msdos data
```
**注意**，msdos会根据data文件的大小自动决定文件系统的格式。通常情况下，如果data < 16MB，则会被格式化成为FAT12。否则会被格式化为FAT16，更大的文件会被格式化为FAT32。为了省事请直接创建32M的空文件。否则（比如你创建了1M的空文件）你需要用`-F 16`指令强制指定文件系统为FAT16。

4.验证该虚拟优盘
```shell
#创建挂载目录
sudo mkdir /dev/sdb1
#挂载虚拟优盘
sudo mount -o umask=000 data /dev/sdb1
#赋予读写权限
sudo chmod -R 777 /dev/sdb1
#卸载虚拟优盘
sudo umount /dev/sdb1
```
挂载虚拟优盘后，从桌面环境里进入sdb1文件夹，可以发现该文件夹剩余空间在31M左右，并且可以像优盘一样使用。**注意**，mount会根据文件大小自动决定是使用FAT12，16，32中的哪种格式挂载，如果如前文所说你使用了比较小的文件作为虚拟优盘，这里可能需要用到`fat=16` 来指定挂载为FAT16。

### 第一周

根据上文描述，你建立的data文件应该在Linux4文件夹中。在进行以下步骤之前，你需要按照上文描述卸载虚拟优盘。

1.打开终端，`cd` 到Linux4文件夹。**Step1：这时，提一次提交你自己的代码。** 每个**Step N** 都是一个检查点，需要提交一次。最后按照步骤给分。**注意** 这些操作记录会被保存在.git文件夹内，如果你误删了.git文件夹，需要重新完成实验。
```shell
git add .
#将学号+Step N 作为注释，提交，例如
git commit -m "13060000 Step 1"
```

**助教，我喜欢Zhuangbility，想用Github #_#...** ，然而助教表示这并没有什么额外加分╯□╰

对于这类同学请参考1.1~1.3，首先你需要设置Github，如果你已经设置好可以略去1.1。如果你不是用ssh而是用的https，那么请自行baidu命令。

1.1设置Github
[添加SSH Key到自己账户](https://help.github.com/articles/generating-ssh-keys/)。**注意** 文中的`pbcopy`是在X OS下才有的命令，Ubuntu用户请自行gedit...

1.2[访问Linux4](https://github.com/tonyshaw/Linux4)，点击右上角Fork，将该工程Fork到你的账户中。

1.3
```shell
git clone git@github.com:你的账户/Linux4.git
#然后在这个Linux4文件夹中完成实验，每完成一步之后，即使用过git commit指令之后，执行以下指令：
git push origin master
#成功之后，你就可以在你自己的repo中看到刚刚push上去的文件，并且Github文件列表中会有你刚刚commit -m后面的文字。
```
2.在Linux4文件夹内执行 `make` 指令。然后执行`./filesys`，程序应当成功编译并且执行。但是发现程序有错误：open failed: Is a directory 。修改该bug，找到`main`函数，发现第一次运行的错误是由于打开文件错误，`open`命令不能打开一个文件路径，所以打开filesys.h，修改`DEVNAME`为data。重新执行`make`，然后运行`./filesys`，程序应该可以正常执行，并且可以尝试运行基本要求中的指令。**Step 2**，这里Step 2的意思是说你需要重新来一遍git add , git commit。以表明你完成了检查点2。整个实验需要提交5次左右，后面我不会单独提醒了！！！不会了！！！我说的很明白了，每个Step都需要提交一边，不得分不能怪助教@_@

3.童鞋们回去以后熟悉一下教材P88，以及我给的两个pdf中的FAT16结构。

### 第二周

1.理解FAT16结构，见PPT。因为markdown插图好麻烦- -

2.修改书中定死的位置信息代码，转而通过引导扇区（Boot Sector）中的信息，计算出所有有用的位置。计算公式请参考FAT16.pdf中Calculation Algorithms这一节。这里我们以`ROOTDIR_OFFSET`（根目录起始地址）为例：
```C++
\\原本.h中的代码：
#define FAT_TWO_OFFSET 512+250*512                       
#define ROOTDIR_OFFSET 512+250*512+250*512+512                     
#define DATA_OFFSET 512+250*512+250*512+512*32   
\\修改后.h中的代码：
#define FAT_TWO_OFFSET 512+250*512                                         
#define DATA_OFFSET 512+250*512+250*512+512*32   
int ROOTDIR_OFFSET = -1;
\\修改后.c中的代码：
void ScanBootSector() {
...
ROOTDIR_OFFSET = bdptor.BytesPerSector + bdptor.FATs * bdptor.SectorsPerFAT * bdptor.BytesPerSector;

printf("Oem_name \t\t%s\n"...
}
```
注意到，这里的计算公式比FAT16.pdf中多了`BytesPerSector`，这是因为我们需要的按照字节计算的开始地址（即从第几个字节起为根目录扇区），而不是按照扇区计算的地址（即从第几个扇区开始为根目录扇区）。最后我们把根目录起始位置计算的结果输出看一看。在`printf` 中添加相关内容。
```c++
printf(...
"HiddenSectors \t\t%d\n"
"ROOTDIR_OFFSET \t\t%d\n",
...,
bdptor.HiddenSectors,
ROOTDIR_OFFSET);
```
**Step 3**，注意这里只需要按照教程修改完`ROOTDIR_OFFSET` 并且在printf中输出即可提交。其余的位置信息请各位按照自己的需求修改，无需打印输出。但会现场检查（例如DATA_OFFSET源代码就有问题- -。。。默默的吐槽一句源代码质量实在是呵呵）。

3.挂载data到`/dev/sdb1`，在图形界面中打开该目录（就是不用命令行啦，用类似于windows的方式进入该目录），然后人工添加一个文件。而后卸载data。在终端中执行`./filesys`，然后输入`ls`，程序应该能正确识别出你刚才添加的文件。**Step 4**

4.理解FAT16如何保存文件。请参见PPT。源代码没有写入创建时间,这里自行修改。最后`int WriteFat()` 也有问题，首先该函数假设了FAT16只有两个File Allocation Tables，所以写回两次。这个显然是不科学的，因为引导区第`0010h` 开始的一个字节，表示FAT的数量，通常情况下，FAT数量为2（包括我们创建的这个data）。但你应当修改该函数，使得他符合标准。 **Step 5**

5.*其他：* 代码要修改的地方很多，鼓励创新，提高要求没有必要全部都做，量力而行做好即可。重要的是通过这个实验理解FAT16这个文件系统结构，并可以对该文件系统进行操作。虽然该结构已经停止使用，但是理解这个结构对于未来理解FAT32，以及其他文件结构都有很大的帮助。

## 提交
请提交到课程网站，无需push到github上。
> By J.Xiao Written with [StackEdit](https://stackedit.io/).