#pragma once

#include "Types.h"

// 수신 버퍼. 읽기/쓰기 커서로 TCP 스트림에서 패킷을 조립한다.
// 내부적으로 BufferSize * BUFFER_COUNT 크기의 여유 공간을 확보하여,
// 패킷이 잘려서 오거나 여러 개가 붙어서 와도 안정적으로 처리한다.
class RecvBuffer
{
	enum { BUFFER_COUNT = 10 };

public:
	RecvBuffer(int32 iBufferSize);

	void Clean();
	bool OnRead(int32 iNumOfBytes);
	bool OnWrite(int32 iNumOfBytes);

	BYTE* ReadPos() { return &Buffer[iReadPos]; }
	BYTE* WritePos() { return &Buffer[iWritePos]; }
	int32 DataSize() { return iWritePos - iReadPos; }
	int32 FreeSize() { return iCapacity - iWritePos; }

private:
	int32 iCapacity = 0;
	int32 iBufferSize = 0;
	int32 iReadPos = 0;
	int32 iWritePos = 0;
	Vector<BYTE> Buffer;
};