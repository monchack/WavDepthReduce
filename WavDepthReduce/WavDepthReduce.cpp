// WavDepthReduce.cpp : コンソール アプリケーションのエントリ ポイントを定義します。
//

#include "stdafx.h"

int getFileSize(wchar_t* fileName, DWORD* sizeLow, DWORD* sizeHigh)
{
	HANDLE h;
	DWORD dwSizeHigh;
	DWORD dwSizeLow;
	DWORD dwError;

	if (sizeLow == 0 || sizeHigh == 0) return 0;
	*sizeLow = *sizeHigh = 0;
	h = CreateFileW(fileName, GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (h == INVALID_HANDLE_VALUE) return 0;
	dwSizeLow = GetFileSize(h, &dwSizeHigh);
	dwError = GetLastError();
	CloseHandle(h);
	if (dwSizeLow == 0xffffffff && dwError != NO_ERROR) return 0;
	*sizeLow = dwSizeLow;
	*sizeHigh = dwSizeHigh;
	return 1;
}

unsigned int searchFmtDataChunk(wchar_t* fileName, WAVEFORMATEX* wf, DWORD* offset, DWORD* size)
{
	HANDLE fileHandle;
	fileHandle = CreateFileW(fileName, GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (fileHandle == INVALID_HANDLE_VALUE)
	{
		return 0;
	}
	
	DWORD header[2];
	DWORD readSize;
	WORD  wav[8];
	DWORD riffSize, pos = 0;
	DWORD dataOffset, dataSize;
	::ReadFile(fileHandle, header, 8, &readSize, NULL);
	bool fmtFound = false, dataFound = false;

	if (readSize != 8)
	{
		CloseHandle(fileHandle);
		return 0;
	}

	if (header[0] != 0X46464952)
	{
		// not "RIFF"
		CloseHandle(fileHandle);
		return 0;
	}
	riffSize = header[1];

	::ReadFile(fileHandle, header, 4, &readSize, NULL);
	if (readSize != 4)
	{
		CloseHandle(fileHandle);
		return 0;
	}
	if (header[0] != 0x45564157)
	{
		// not "WAVE"
		CloseHandle(fileHandle);
		return 0;
	}
	pos += 4;

	while (pos < riffSize)
	{
		::ReadFile(fileHandle, header, 8, &readSize, NULL);
		if (readSize != 8)
		{
			break;
		}
		pos += 8;

		if (header[0] == 0X20746d66)
		{
			// "fmt "
			if (header[1] >= 16)
			{
				::ReadFile(fileHandle, wav, 16, &readSize, NULL);
				if (readSize != 16)
				{
					break;
				}
				fmtFound = true;
				if (header[1] > 16)
				{
					::SetFilePointer(fileHandle, header[1] - 16, 0, FILE_CURRENT);
				}
				pos += header[1];
			}
			else
			{
				::SetFilePointer(fileHandle, header[1], 0, FILE_CURRENT);
				pos += header[1];
			}
		}
		else if (header[0] == 0X61746164)
		{
			// "data"
			dataFound = true;
			dataOffset = ::SetFilePointer(fileHandle, 0, 0, FILE_CURRENT);
			dataSize = header[1];
			::SetFilePointer(fileHandle, header[1], 0, FILE_CURRENT);
			pos += header[1];
		}
		else
		{
			::SetFilePointer(fileHandle, header[1], 0, FILE_CURRENT);
			pos += header[1];
		}
		if (GetLastError() != NO_ERROR)
		{
			break;
		}
	}
	CloseHandle(fileHandle);

	if (dataFound && fmtFound)
	{
		*offset = dataOffset;
		*size = dataSize;
		wf->wFormatTag = wav[0]; //  1:LPCM   3:IEEE float
		wf->nChannels = wav[1]; //  1:Mono  2:Stereo
		wf->nSamplesPerSec = *(DWORD* )(wav+ 2);  // 44100, 48000, 176400, 19200, 352800, 384000...
		wf->nAvgBytesPerSec = *(DWORD* )(wav + 4); 
		wf->nBlockAlign = wav[6]; // 4@16bit/2ch,  6@24bit/2ch,   8@32bit/2ch   
		wf->wBitsPerSample = wav[7]; // 16bit, 24bit, 32bit
		wf->cbSize = 0;
		return 1;
	}
	return 0;
}

void* readWavFile(wchar_t* fileName)
{
	HANDLE fileHandle;
	DWORD offset, size, readSize;
	void* data;
	WAVEFORMATEX wf;

	if (!searchFmtDataChunk(fileName, &wf, &offset, &size))
	{
		return 0;
	}

	fileHandle = CreateFileW(fileName, GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (fileHandle == INVALID_HANDLE_VALUE)
	{
		return 0;
	}

	data = (void*) ::GlobalAlloc(GPTR, ((size + 15) / 16) * 16);

	::SetFilePointer(fileHandle, offset, 0, FILE_CURRENT);
	::ReadFile(fileHandle, data, size, &readSize, NULL);
	::CloseHandle(fileHandle);

	return data;
}

int writeWAV_header(HANDLE fileHandle, int ch, int freq, int depth,  unsigned long dataSize)
{
	if (fileHandle == 0) return 0;
	if (ch != 1 && ch != 2) return 0;
	if (depth != 16 && depth != 24 && depth != 32) return 0;

	WAVEFORMATEX wf;
	wf.wFormatTag = 0x01; // 1:LPCM
	wf.nChannels = ch;
	wf.nSamplesPerSec = freq;
	wf.nAvgBytesPerSec = freq * ((depth * ch) / 8);
	wf.nBlockAlign = (depth * ch) / 8; // 4bytes (16bit, 2ch) per sample
	wf.wBitsPerSample = depth;
	wf.cbSize = 0;

	DWORD writtenSize = 0;
	WriteFile(fileHandle, "RIFF", 4, &writtenSize, NULL);
	DWORD size = (dataSize + 44) - 8;
	WriteFile(fileHandle, &size, 4, &writtenSize, NULL);
	WriteFile(fileHandle, "WAVE", 4, &writtenSize, NULL);
	WriteFile(fileHandle, "fmt ", 4, &writtenSize, NULL);
	size = 16;
	WriteFile(fileHandle, &size, 4, &writtenSize, NULL);
	WriteFile(fileHandle, &wf, size, &writtenSize, NULL);
	WriteFile(fileHandle, "data", 4, &writtenSize, NULL);
	size = (DWORD)dataSize;
	WriteFile(fileHandle, &size, 4, &writtenSize, NULL);

	return 0;
}

// X次 ノイズシェーパー
void noiseShaper(wchar_t* destFileName, void* data, int sample, WAVEFORMATEX* wf, int x)
{
	HANDLE fileHandle = CreateFileW(destFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS /*CREATE_NEW*/, FILE_ATTRIBUTE_NORMAL, NULL);
	writeWAV_header(fileHandle, 2, wf->nSamplesPerSec, 16, sample * 2 * 2);

	short* data2 = (short*)::GlobalAlloc(GPTR, sample * 2 * 2);

	BYTE* p = (BYTE*)data;
	int left, right, add =  1 << 7;
	DWORD writtenSize;

	int lastRight = 0;
	int lastLeft = 0;
	int outRight;
	int outLeft;
	long long sigmaLeft[32];
	long long sigmaRight[32];

	short n = -1;

	for (int i = 0; i < 32; ++i)
	{
		sigmaLeft[i] = sigmaRight[i] = 0;
	}

	for (int i = 0; i < sample; ++i)
	{
		unsigned int tmp;
		tmp = (p[2] << 16) | (p[1] << 8) | p[0];
		if (tmp > 0x00800000)
		{
			left = (0x01000000 - tmp) * -1;
		}
		else left = (int)tmp;

		tmp = (p[5] << 16) | (p[4] << 8) | p[3];
		if (tmp > 0x00800000)
		{
			right = (0x01000000 - tmp) * -1;
		}
		else right = (int)tmp;
	
		sigmaLeft[0] += left - lastLeft;
		sigmaRight[0] += right - lastRight;
		for (int j = 1; j < x; ++j)
		{
			sigmaLeft[j] += sigmaLeft[j - 1] - lastLeft;
			sigmaRight[j] += sigmaRight[j - 1] - lastRight;
		}

		if (sigmaLeft[x - 1] >= 0)
		{
			outLeft = (sigmaLeft[x - 1] + add) >> 8;
			lastLeft = outLeft << 8;
		}
		else
		{
			outLeft = (sigmaLeft[x - 1] * -1 + add) >> 8;
			lastLeft = outLeft << 8;
			outLeft *= -1;
			lastLeft *= -1;
		}

		///// right
		if (sigmaRight[x - 1] >= 0)
		{
			outRight = (sigmaRight[x -1] + add) >> 8;
			lastRight = outRight << 8;
		}
		else
		{
			outRight = (sigmaRight[x - 1] * -1 + add) >> 8;
			lastRight = outRight << 8;
			outRight *= -1;
			lastRight *= -1;
		}

		if (outLeft > 32767) outLeft = 32767;
		if (outLeft < -32768) outLeft = -32768;

		if (outRight > 32767) outRight = 32767;
		if (outRight < -32768) outRight = -32768;

		data2[i * 2] = (short)outLeft;
		data2[i * 2 + 1] = (short)outRight;

		p += 6;
	}

	::WriteFile(fileHandle, data2, sample * 2 * 2, &writtenSize, NULL);
	::FlushFileBuffers(fileHandle); // なくても大丈夫そう
	::CloseHandle(fileHandle);

	::GlobalFree(data2);
}

int _tmain(int argc, _TCHAR* argv[])
{
	WAVEFORMATEX wf;
	DWORD offset, size, writtenSize = 0;
	void* data = 0;
	void* output = 0;

	wchar_t fileName[]= L"C:\\Test\\1k_174_24.wav";
	wchar_t destFileName[] = L"C:\\Test\\out.wav";
	
	searchFmtDataChunk(fileName, &wf, &offset, &size);
	data = readWavFile(fileName);


	int sample = size / ((wf.wBitsPerSample * wf.nChannels) / 8);
	if (wf.wBitsPerSample == 24 && wf.nChannels == 2)
	{
		noiseShaper(destFileName, data, sample, &wf, 12);
	}

	if (data) {
		::GlobalFree(data);
		data = 0;
	}

	if (output) {
		::GlobalFree(output);
		data = 0;
	}

	return 0;
}

