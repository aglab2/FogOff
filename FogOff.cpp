#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef _WIN32
#include <algorithm>
#include <windows.h>
#define read _read
#define close _close

static int posixOpen(const char* path, int flags, int mode = 0)
{
	int fd;
	errno = _sopen_s(&fd, path, flags, _SH_DENYNO, mode);
	if (errno)
	{
		return -1;
	}
	else
	{
		return fd;
	}
}

static unsigned int posixPRead(int fd, void* to, size_t size, uint64_t off)
{
	constexpr size_t kMax = (size_t)(1UL << 31);
	DWORD reading = static_cast<DWORD>(std::min<std::size_t>(kMax, size));
	DWORD ret;
	OVERLAPPED overlapped;
	memset(&overlapped, 0, sizeof(OVERLAPPED));
	overlapped.Offset = static_cast<DWORD>(off);
	overlapped.OffsetHigh = static_cast<DWORD>(off >> 32);
	if (!ReadFile((HANDLE)_get_osfhandle(fd), to, reading, &ret, &overlapped))
	{
		return -1;
	}

	return static_cast<unsigned int>(ret);
}

static unsigned int posixPWrite(int fd, const void* buf, size_t size, uint64_t off)
{
	constexpr size_t kMax = (size_t)(1UL << 31);
	DWORD reading = static_cast<DWORD>(std::min<std::size_t>(kMax, size));
	DWORD ret;
	OVERLAPPED overlapped;
	memset(&overlapped, 0, sizeof(OVERLAPPED));
	overlapped.Offset = static_cast<DWORD>(off);
	overlapped.OffsetHigh = static_cast<DWORD>(off >> 32);
	if (!WriteFile((HANDLE)_get_osfhandle(fd), buf, reading, &ret, &overlapped))
	{
		return -1;
	}

	return static_cast<unsigned int>(ret);
}

#else
#define posixOpen open
#define posixPRead pread
#define posixPWrite pwrite
#endif

union Cmd
{
	struct
	{
		uint8_t op;
		uint8_t buf[7];
	};
	struct
	{
		uint32_t w0;
		uint32_t addr;
	};
};

static bool operator==(const Cmd& c1, const Cmd& c2)
{
	return 0 == memcmp(&c1, &c2, 8);
}

static bool operator!=(const Cmd& c1, const Cmd& c2)
{
	return 0 != memcmp(&c1, &c2, 8);
}

constexpr Cmd FinalCombiner = { 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0x79, 0x3C };
constexpr Cmd Layer1Combiner = { 0xFC, 0x12, 0x7E, 0x24, 0xFF, 0xFF, 0xF9, 0xFC };
constexpr Cmd Layer4Combiner = { 0xFC, 0x12, 0x7E, 0x24, 0xFF, 0xFF, 0xF3, 0xF9 };
constexpr Cmd NOP = { 0xB7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

int main(int argc, char* argv[])
{
	if (argc < 4)
	{
		printf("%s PATH OFFSET LAYER\n", argv[0]);
		exit(1);
	}

	int fd = posixOpen(argv[1], O_RDWR);
	int off;
	{
		char* p;
		off = strtol(argv[2], &p, 16);
	}
	int layer = atoi(argv[3]);
	const Cmd* layerCmd = layer == 1 ? &Layer1Combiner : &Layer4Combiner;

	for (;; off += 8)
	{
		Cmd cmd;
		if (8 != posixPRead(fd, &cmd, 8, off))
		{
			break;
		}

		if (0xb8 == cmd.op)
		{
			break;
		}

		if (0xfc == cmd.op)
		{
			if (cmd != FinalCombiner)
			{
				posixPWrite(fd, layerCmd, 8, off);
			}
		}

		if (0xba == cmd.op || 0xf8 == cmd.op || 0xB9 == cmd.op || 0xBC == cmd.op || 0xB7 == cmd.op)
		{
			posixPWrite(fd, &NOP, 8, off);
		}
	}
	close(fd);
}
