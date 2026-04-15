# vgm2wav2

VGM/S98/DRO/GYM/NSF/SPC 등 게임 음악 파일을 WAV/MP3/AAC 등으로 변환하는 CLI 도구.
[libvgm](https://github.com/ValleyBell/libvgm)과 [libgme](https://github.com/libgme/game-music-emu)를 기반으로 하며, 단일 파일 외에 **폴더**와 **ZIP 아카이브** 일괄 변환을 지원합니다.

## 지원 형식

### 입력

| 형식 | 엔진 | 설명 |
|------|------|------|
| `.vgm` / `.vgz` | libvgm | Video Game Music (gzip 압축 자동 감지) |
| `.s98` | libvgm | PC-88/PC-98 등 |
| `.dro` | libvgm | DOSBox Raw OPL |
| `.gym` | libvgm | Genesis YM2612 |
| `.nsf` / `.nsfe` | GME | NES/Famicom |
| `.spc` | GME | SNES/Super Famicom |
| `.gbs` | GME | Game Boy |
| `.ay` | GME | ZX Spectrum / Amstrad CPC |
| `.hes` | GME | PC-Engine/TurboGrafx-16 |
| `.kss` | GME | MSX / SMS |
| `.sap` | GME | Atari |
| 디렉토리 | — | 하위 폴더까지 재귀 탐색 |
| `.zip` | — | ZIP 내 지원 파일 일괄 변환 |

### 출력

| 포맷 | `--format` 값 | 의존성 |
|------|--------------|--------|
| WAV  | `wav` (기본값) | 없음 |
| MP3  | `mp3` | ffmpeg |
| AAC  | `aac` | ffmpeg |
| FLAC | `flac` | ffmpeg |
| 기타 | ffmpeg 지원 포맷 이름 | ffmpeg |

## 빌드

### 의존성

- CMake 3.12+
- C++17 컴파일러
- zlib (libvgm 의존성, macOS 기본 포함)
- [libzip](https://libzip.org/) — ZIP 지원 시 필요 (선택)
- [libgme](https://github.com/libgme/game-music-emu) — GME 엔진 지원 시 필요 (선택)
- [ffmpeg](https://ffmpeg.org/) — WAV 이외 포맷 출력 시 필요 (런타임)

libzip, libgme 없이도 빌드 및 실행 가능합니다. 각 기능 사용 시에만 에러가 출력됩니다.

macOS (Homebrew):
```bash
brew install libzip game-music-emu ffmpeg
```

### 빌드

macOS / Linux:
```bash
git clone --recurse-submodules https://github.com/yourname/vgm2wav2
cd vgm2wav2
cmake -B build -S .
cmake --build build
```

Windows (MSYS2 MinGW64 또는 PowerShell):
```powershell
git clone --recurse-submodules https://github.com/yourname/vgm2wav2
cd vgm2wav2
cmake -B build -S . -G "MinGW Makefiles"
cmake --build build --target vgm2wav2
```

> MSYS2가 없으면 [msys2.org](https://www.msys2.org)에서 설치 후 MinGW64 패키지를 설치합니다:
> ```bash
> pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
> ```

빌드 시 libgme가 감지되면 `libgme found: GME engine support enabled` 메시지가 출력됩니다.

빌드 결과물:
- `build/vgm2wav2` (Linux/macOS) / `build/vgm2wav2.exe` (Windows)
- `build/libvgm/bin/vgm2wav` — libvgm 원본 (단일 파일 전용)

## TUI 플레이어

터미널에서 직접 재생할 수 있는 Textual 기반 TUI 플레이어입니다.

- 게임 음악(VGM/SPC/NSF 등) — `vgm2wav2 --stdout` 으로 PCM 스트리밍
- 일반 오디오(MP3/WAV/FLAC/AAC/OGG/OPUS/M4A) — `ffmpeg` 으로 디코딩
- FFT 스펙트럼 시각화 (4행, 로그 스케일)
- M3U 재생목록 저장 · 불러오기
- 파일 탐색기에서 다중 선택 → 재생목록 추가 / 일괄 변환

### 의존성 설치

macOS / Linux:
```bash
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

Windows:
```powershell
python -m venv .venv
.venv\Scripts\pip install -r requirements.txt
```

### 실행

macOS / Linux:
```bash
# 단일 파일
.venv/bin/python player.py bgm.vgm

# M3U 재생목록
.venv/bin/python player.py playlist.m3u

# 여러 파일
.venv/bin/python player.py *.spc
```

Windows:
```powershell
# 단일 파일
.venv\Scripts\python player.py bgm.vgm

# M3U 재생목록
.venv\Scripts\python player.py playlist.m3u

# 여러 파일
.venv\Scripts\python player.py (Get-Item *.spc)
```

### 키 조작

#### 재생 제어

| 키 | 동작 |
|----|------|
| `Space` | 재생 / 일시정지 (파일 탐색기에서는 파일 선택/해제) |
| `N` | 다음 곡 |
| `P` | 이전 곡 (3초 이내: 현재 곡 처음부터) |
| `Enter` | 재생목록 / 탐색기에서 선택한 곡 재생 |
| `Delete` | 재생목록에서 선택한 곡 삭제 |

#### 파일 탐색기

| 키 | 동작 |
|----|------|
| `Space` | 파일 다중 선택 / 해제 (✓ 표시) |
| `Esc` | 선택 전체 해제 |
| `A` | 선택된 파일을 재생목록에 추가 |
| `C` | 선택된 파일 일괄 변환 (포맷·출력 디렉토리 선택) |
| `Backspace` | 상위 폴더로 이동 |
| `G` | 경로 직접 입력으로 이동 |

파일 탐색기는 파일 종류별로 색상을 구분합니다:

| 색상 | 종류 |
|------|------|
| 밝은 청록 | 게임 음악 (VGM/SPC/NSF 등) |
| 밝은 노랑 | 일반 오디오 (MP3/WAV/FLAC 등) |
| 밝은 자홍 | 재생목록 (.m3u) |

상단 필터 바(VGM / Audio / M3U)로 표시할 파일 종류를 선택적으로 켜고 끌 수 있습니다.

#### 기타

| 키 | 동작 |
|----|------|
| `L` / `F` | 재생목록 ↔ 파일 탐색기 전환 |
| `S` | 재생목록을 M3U 파일로 저장 |
| `H` | 도움말 |
| `Q` | 종료 |

### 일괄 변환

파일 탐색기에서 `Space`로 여러 파일을 선택한 뒤 `C`를 누르면 변환 창이 열립니다.

- **출력 형식**: MP3(기본) / WAV / AAC / FLAC / OGG / OPUS
- **출력 디렉토리**: 기본값 `./output` (자동 생성)
- **변환 후 재생목록에 추가** 옵션으로 변환된 파일을 바로 재생 가능
- 게임 음악 포맷(VGM/SPC 등)만 변환 가능; 일반 오디오 파일은 자동 스킵

### 포터블 바이너리 다운로드 (권장)

[GitHub Releases](https://github.com/honux77/vgm2wav2/releases) 에서 운영체제에 맞는 zip 파일을 다운로드하세요.
Python이나 CMake 설치 없이 바로 실행할 수 있습니다.

| 파일 | 대상 |
|------|------|
| `vgm-player-macos-arm64.zip` | Apple Silicon Mac (M1/M2/M3) |
| `vgm-player-macos-x86_64.zip` | Intel Mac |
| `vgm-player-windows-x64.zip` | Windows 64-bit |

압축 해제 후 `vgm-player` 폴더 안의 실행 파일을 실행합니다.

> **ffmpeg 필요** — MP3/AAC/FLAC 변환 및 일반 오디오 재생에 필요합니다.
> - macOS: `brew install ffmpeg`
> - Windows: [ffmpeg.org](https://ffmpeg.org/download.html) 다운로드 후 PATH 등록

> **macOS 첫 실행 시** Gatekeeper 경고가 나오면:
> 시스템 설정 → 개인 정보 보호 및 보안 → "확인 없이 열기" 클릭

### 직접 빌드 (개발자용, PyInstaller)

소스에서 패키징하려면:

```bash
# vgm2wav2 C++ 바이너리를 먼저 빌드한 후
pip install pyinstaller
pyinstaller --onedir --name vgm-player \
  --collect-all textual \
  --add-binary "build/vgm2wav2:." \
  player.py
# 결과물: dist/vgm-player/
```

### vgmplay 커맨드 설치

PATH에 등록된 `vgmplay` 커맨드로 어디서든 간편하게 실행할 수 있습니다.

#### macOS / Linux

`~/.local/bin`에 래퍼 스크립트를 생성합니다. `/path/to/vgm2wav2`는 실제 클론 경로로 수정하세요.

```bash
cat > ~/.local/bin/vgmplay <<'EOF'
#!/bin/sh
exec /path/to/vgm2wav2/.venv/bin/python /path/to/vgm2wav2/player.py "$@"
EOF
chmod +x ~/.local/bin/vgmplay
```

`~/.local/bin`이 PATH에 없으면 셸 설정 파일(`~/.zshrc` 또는 `~/.bashrc`)에 추가합니다:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

#### Windows (PowerShell)

PowerShell 프로파일(`$PROFILE`)에 함수를 추가합니다:

```powershell
function vgmplay { & C:\path\to\vgm2wav2\.venv\Scripts\python C:\path\to\vgm2wav2\player.py @args }
```

#### 설치 확인 및 사용법

```bash
vgmplay --help

# 파일 브라우저로 열기 (인수 없이 실행)
vgmplay

# 파일 직접 지정
vgmplay bgm.vgm

# M3U 재생목록
vgmplay playlist.m3u

# 여러 파일
vgmplay *.spc

# 루프·페이드 옵션
vgmplay --loops 2 --fade 5 bgm.vgm
```

## 변환 사용법

```
vgm2wav2 [options] <input> [output]

  input   : 오디오 파일, 디렉토리, 또는 .zip 아카이브
  output  : 오디오 파일 (단일 파일 입력) 또는 디렉토리 (폴더/ZIP 입력)
            (--play / --stdout 사용 시 생략 가능)

Options:
  --play           터미널에서 직접 재생 (ffplay 필요)
  --stdout         raw PCM (s16le 44100 stereo) 을 stdout으로 출력
  --format fmt     출력 포맷: wav, mp3, aac, flac, ... (기본값: wav)
  --engine e       엔진 선택: auto, libvgm, gme (기본값: auto)
  --samplerate n   샘플레이트 (기본값: 44100)
  --bps n          비트 심도: 16 / 24 / 32 (기본값: 16; GME 엔진에서는 무시됨)
  --fade x         페이드아웃 길이, 초 단위 (기본값: 8.0)
  --loops n        루프 횟수 (기본값: 2)
  --skip           출력 파일이 이미 존재하면 건너뜀
  --dryrun         실제 변환 없이 동작만 출력
```

### 엔진 선택

확장자에 따라 엔진이 자동 선택됩니다:

- `.vgm` / `.vgz` / `.s98` / `.dro` / `.gym` → libvgm
- `.nsf` / `.spc` / `.gbs` / `.ay` / `.hes` / `.kss` / `.sap` / `.nsfe` → GME

`--engine gme` 또는 `--engine libvgm`으로 강제 지정할 수 있습니다.

NSF 등 다중 트랙 파일은 모든 트랙을 자동으로 추출합니다 (`game_01.wav`, `game_02.wav`, ...).

### 예시

```bash
# 단일 파일 (WAV)
vgm2wav2 bgm.vgm bgm.wav
vgm2wav2 game.spc game.wav

# NES 음악 전체 트랙 추출
vgm2wav2 game.nsf output/
# → output/game_01.wav, output/game_02.wav, ...

# MP3 / AAC 변환
vgm2wav2 --format mp3 bgm.vgz bgm.mp3
vgm2wav2 --format flac game.spc game.flac

# 폴더 일괄 변환 (디렉토리 구조 유지)
vgm2wav2 --format mp3 music/ output/

# ZIP 아카이브
vgm2wav2 --format aac songs.zip output/

# 엔진 강제 지정
vgm2wav2 --engine gme game.vgm game.wav

# 옵션 지정
vgm2wav2 --format mp3 --samplerate 48000 --loops 1 --fade 3.0 bgm.vgm bgm.mp3
```

폴더 및 ZIP 입력 시 출력 디렉토리가 없으면 자동 생성되며, 원본 하위 디렉토리 구조가 그대로 유지됩니다.

WAV 이외 포맷은 렌더링한 PCM을 ffmpeg에 파이프로 전달해 인코딩합니다. ffmpeg가 설치되어 있지 않으면 시작 시 에러가 출력됩니다.

## 라이선스

libvgm의 라이선스를 따릅니다.
