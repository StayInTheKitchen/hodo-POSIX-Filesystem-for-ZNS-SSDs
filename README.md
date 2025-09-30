
### 1. 프로젝트 배경
본 프로젝트 **hodo**[hodu(호두)] 파일시스템은 ZNS SSD를 지원하는 POSIX 파일시스템입니다.


#### 1.1. 기존 ZNS SSD 지원 파일시스템의 한계
1.  f2fs
> f2fs는 메타데이터 저장을 위해서 random write zone이 필요하기에 순수 ZNS SSD에서 동작하지 않습니다.
2. zonefs
> zonefs는 하나의 zone을 하나의 파일로 노출하는 단순한 파일시스템이기에 POSIX 파일시스템 API를 제공하지 않아 사용성이 낮습니다.

#### 1.2. 필요성과 기대효과
> **hodo** 파일시스템을 활용하면 기존의 파일 관련 셸 명령어와 응용 프로그램을 별도의 수정 없이 순수 ZNS SSD 환경에서 구동할 수 있어, 시스템의 **호환성과 사용성**을 크게 향상시킬 수 있습니다.

---

### 2. 개발 목표
#### 2.1. 순수 ZNS SSD를 지원하는 POSIX 파일시스템 개발
> POSIX 파일시스템 operation을 구현해 VFS와 인터페이스하며 POSIX API를 지원하도록 개발했습니다.

구현한 함수 목록은  졸업과제 보고서의 `3.2.7 기능별 구현 함수` 참고.

#### 2.2. garbage collection 개발
> 유효하지 않은 블록(garbage)을 없애 유효 저장공간을 확보하는 기능을 개발했습니다.

---

### 3. 시스템 설계
#### 3.1. 시스템 설계 개요
> **hodo** 파일 시스템은 zonefs 파일 시스템 모듈을 확장하여 구현한 로그 구조 파일 시스템입니다.

#### 3.2. 시스템 주요 구성 요소
1. mapping  table
아이노드 또는 데이터 블록의 논리 주소와 물리 주소의 매핑 관계를 표현합니다.

2. 쓰기 포인터
ZNS SSD 장치에서 다음에 쓰여질 물리 주소를 가리키는 역할을 합니다.

3. multi-level indexing scheme
약 4TB의 최대 파일 크기를 지원합니다. 
 
#### 3.3. 사용 기술
-   **프로그래밍 언어**: C 언어

-   **개발 환경**: Linux Kernel 6.8 기반, FEMU(ZNS SSD 에뮬레이터)

-   **관련 기술**: VFS(Virtual File System) 인터페이스, zonefs

-   **도구**: `make`, `gcc`, `gdb`, `fio` 등을 이용한 컴파일 및 성능 검증

---

### 4. 개발 결과

#### 4.1. 전체 시스템 흐름도

-   **Application (사용자 프로그램)**
    
    -   사용자 공간에서 실행되는 응용 프로그램이 `open()`, `read()`, `write()`, `mkdir()` 같은 **POSIX 시스템 콜**을 호출합니다.
        
    -   예: `cat a.txt`, `ls`, `echo "aaa" > a.txt`
        
-   **VFS (Virtual File System)**
    
    -   리눅스 커널 안에 있는 **추상화 계층**으로, 여러 파일시스템(f2fs, ext4, zonefs, hodo 등)을 공통된 인터페이스로 연결합니다.
        
    -   시스템 콜로 들어온 파일 연산을 이 요청을 처리할 구체적인 파일시스템 구현체 호출을 통해서 처리합니다.
        
-   **파일시스템 구현체**
    
    -   VFS가 넘겨준 요청을 실제 파일시스템 모듈이 처리합니다.
        
    -   hodo: zonefs를 확장하여 POSIX API를 지원하는 본 프로젝트의 구현체
        
-   **저장장치 (ZNS SSD)**
    
    -   실제 물리적 데이터가 저장되는 곳입니다.
        
    -   hodo 파일시스템은 **순차 쓰기 제약**을 고려하여 ZNS SSD에 효율적으로 데이터/메타데이터를 배치합니다.

#### 4.2. 기능 설명 및 주요 기능 명세서

1.  **계층적 디렉토리 구조**
    
    -   단일 파일 단위로만 접근 가능한 기존 zonefs를 확장하여, 디렉토리와 서브 디렉토리를 지원한다.
        
    -   사용자는 `mkdir`, `rmdir`, `ls` 등의 명령어를 통해 일반적인 계층적 파일 관리가 가능하다.
        
2.  **파일 조작 기능**
    
    -   **읽기/쓰기**: `read()`, `write()` 시스템콜을 통해 POSIX 인터페이스 기반의 파일 입출력이 가능하다.
        
    -   **생성/삭제**: `open(O_CREAT)`, `unlink()` 등을 통한 파일 생성 및 삭제 기능을 지원한다.
        
    -   이를 통해 응용 프로그램은 수정 없이 ZNS SSD에서 동작할 수 있다.
        
3.  **Garbage Collection 기능**
    
    -   ZNS SSD의 순차 쓰기 제약으로 인해 불필요한 데이터가 발생하면, 이를 회수하여 장치 공간을 효율적으로 활용한다.
        
    -   유효한 데이터를 reserved zone으로 임시 이동(swap-in) 후, 앞쪽 zone을 초기화하고 다시 원위치(swap-out)하는 방식으로 구현하였다.
        
    -   이를 통해 장치 수명을 연장하고 성능을 안정적으로 유지한다.

#### 4.3. 산업체 멘토링 의견 및 반영 사항
- 멘토링 의견서 내용
> 보조목표인 zone 할당 정책에 대한 구체적인 설계와 구현에 대한 내용이 부족. 즉, 가비지 컬렉션과 웨어레벨링을 구체적으로 어떤 방식으로 개선할지 간단히 방향성을 제시
- 반영 사항
> garbage collection 기능을 구현하였으며, 동적 zone 할당 정책 방향성을 제시하며 wear-leveling 관리 방식을 보고서에 반영하였습니다.

---

### 5. 설치 및 실행 방법
> 본 프로젝트는 Linux 커널 버전 6.8에서 구동 가능합니다.

#### 5.1. 설치절차 및 실행 방법

#### Part 0: FEMU 가상환경 설정
> ZNS SSD 에뮬레이션을 위해 FEMU 환경이 필요합니다.

##### 0-1. 이미지 준비
```bash
wget https://huggingface.co/datasets/hgft1/femu-image/resolve/main/femu24ubuntu.qcow2
mkdir -p ~/images
mv femu24ubuntu.qcow2 ~/images/u20s.qcow2
```

##### 0-2. FEMU 설치
```bash
cd ~
git clone https://github.com/vtess/femu.git
cd femu
mkdir build-femu
cd build-femu

cp ../femu-scripts/femu-copy-scripts.sh .
./femu-copy-scripts.sh .

sudo ./pkgdep.sh
./femu-compile.sh
```

##### 0-3. FEMU 실행
```bash
cd ~/femu/build-femu
./run-zns.sh
```
-   QEMU 가상머신이 부팅되며, SSH 포트는 `localhost:8080`으로 연결됩니다.
-   로그인 계정은 다음과 같습니다    
```bash
ID: ubuntu
PW: ubuntu
```

#### 번외: 작업 공간 확보
> 커널 빌드는 20~30GB 이상의 여유 공간이 필요합니다. 부족한 경우, 아래 방법으로 LVM 기반 루트 파티션을 확장할 수 있습니다.

##### 0-1. 현재 디스크 용량 확인
```bash
df -h
lsblk
```
##### 0-2. LVM 사용 중이고 여유 공간이 있다면
```bash
sudo vgdisplay
```
출력 결과의 `Free PE / Size` 항목을 확인하세요.
##### 0-3. 루트 파티션 확장 (예: +20G)

```bash
sudo lvextend -L +20G /dev/mapper/ubuntu--vg-ubuntu--lv
sudo resize2fs /dev/mapper/ubuntu--vg-ubuntu--lv
```

#### Part 1: GitHub에서 커널 v6.8 다운로드 및 전체 빌드

##### 1-1. 의존 패키지 설치

```bash
sudo apt update
sudo apt install -y build-essential libncurses-dev bison flex libssl-dev libelf-dev git
```

##### 1-2. GitHub에서 커널 소스(v6.8) 클론
```bash
git clone --depth=1 --branch v6.8 https://github.com/torvalds/linux.git linux-6.8
cd linux-6.8
```
##### 1-3. 현재 시스템 커널 설정 복사 및 적용
```bash
cp /boot/config-$(uname -r) .config
make olddefconfig
```

####  모듈 서명 오류 방지를 위한 설정 변경
> `canonical-certs.pem` 오류 방지를 위해 `.config`를 아래처럼 수정하세요:

```bash
sed -i 's/^CONFIG_MODULE_SIG=.*/CONFIG_MODULE_SIG=n/' .config || echo "CONFIG_MODULE_SIG=n" >> .config
sed -i 's/^CONFIG_SYSTEM_TRUSTED_KEYS=.*/CONFIG_SYSTEM_TRUSTED_KEYS=\"\"/' .config || echo 'CONFIG_SYSTEM_TRUSTED_KEYS=""' >> .config
sed -i 's/^CONFIG_SYSTEM_REVOCATION_KEYS=.*/CONFIG_SYSTEM_REVOCATION_KEYS=\"\"/' .config || echo 'CONFIG_SYSTEM_REVOCATION_KEYS=""' >> .config

make olddefconfig
```


##### 1-4. 전체 커널 + 모듈 빌드
```bash
make -j$(nproc)
make modules -j$(nproc)
```

##### 1-5. 모듈 및 커널 설치
```bash
sudo make modules_install
sudo make install
```
##### 1-6. 부팅 항목 등록 및 재부팅
```bash
sudo sed -i 's/^GRUB_DEFAULT=.*/GRUB_DEFAULT="Advanced options for Ubuntu>Ubuntu, with Linux 6.8.0"/' /etc/default/grub
sudo update-grub
sudo reboot
```
> 재부팅 후 `uname -r` → `6.8.x` 확인

#### Part 2: hodo 파일시스템 (zonefs 모듈) 설치/컴파일/적용
##### 2-1. hodo 파일시스템 설치
```bash
cd ~/linux-6.8/fs/zonefs
git clone https://github.com/StayInTheKitchen/hodo-POSIX-Filesystem-for-ZNS-SSDs.git .
```

##### 2-2. hodo 파일시스템 (zonefs 모듈) 컴파일
```bash
cd ~/linux-6.8
make M=fs/zonefs -j$(nproc)
```

##### 2-2. 모듈 제거 및 재삽입
```bash
sudo umount /mnt
sudo rmmod zonefs
sudo insmod fs/zonefs/zonefs.ko
```
> `sudo rmmod zonefs` 로 모듈이 제거가 안 될 경우 `sudo modprobe -r zonefs` 로 의존 모듈도 함께 제거하세요.

##### 2-3. hodo 파일시스템 마운트
```bash
sudo mkzonefs -f /dev/nvme0n1
sudo mount -t zonefs /dev/nvme0n1 /mnt
```

---

### 6. 소개 자료 및 시연 영상
#### 6.1. 프로젝트 소개 영상
> 
#### 6.2. 시연 영상
> 

---

### 7. 팀 구성
#### 7.1. 팀원별 역할 분담

| 구분     | 역할 |
|----------|------|
| **김민준** | 1. 초기화 기능 구현 <br> 2. 파일 생성 기능 구현 <br> 3. 디렉토리 생성 기능 구현 <br> 4. 파일 속성 변경 기능 구현 <br> 5. 파일 읽기 기능 구현 |
| **박천휘** | 1. 파일 존재 확인 기능 구현 <br> 2. 디렉토리 내 하위 파일 목록 확인 기능 구현 <br> 3. 파일 삭제 기능 구현 <br> 4. 디렉토리 삭제 기능 구현 <br> 5. 파일 쓰기 기능 구현 |
| **공통**   | 1. zonefs 동작 분석 <br> 2. 파일시스템 구조 설계 <br> 3. GC 관련 설계 개선 및 구현 <br> 4. 개발 기록 및 보고서 작성 |

#### 7.2. 팀원 별 참여 후기
> 개별적으로 느낀 점, 협업, 기술적 어려움 극복 사례 등

### 8. 참고 문헌 및 출처
``` plaintext
[1] Bjørling, M., Aghayev, A., Holmberg, H., Ramesh, A., Le Moal, D., Ganger, G. R., & Amvrosiadis, G., "Avoiding the block interface tax for flash-based", 2021 USENIX annual technical conference (USENIX ATC 21), pp. 689-703, 2021. 31 
[2] Western Digital, "Zoned Storage File Systems," [Online]. Available: https://zonedstorage.io/docs/filesystems. (Accessed: Sept. 13, 2025) 
[3] Jens Axboe. (2017). "fio: Flexible I/O Tester," [Online]. Available: https://fio.readthedocs.io/en/latest/fio_doc.html (Accessed: 2025, Sep. 13)
```
