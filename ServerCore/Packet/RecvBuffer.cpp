#include "RecvBuffer.h"

// 버퍼 크기의 BUFFER_COUNT배만큼 공간을 확보한다.
RecvBuffer::RecvBuffer(int32 iBufferSize)
	: iBufferSize(iBufferSize)
{
	iCapacity = iBufferSize * BUFFER_COUNT;
	Buffer.resize(iCapacity);
}

// 여유 공간이 부족하면 남은 데이터를 버퍼 앞쪽으로 이동시킨다.
void RecvBuffer::Clean()
{
	int32 iDataSize = DataSize();
	if (iDataSize == 0)
	{
		// 읽기/쓰기 커서가 같은 위치면 둘 다 0으로 리셋
		iReadPos = iWritePos = 0;
	}
	else
	{
		// 남은 여유 공간이 패킷 1개 크기 미만이면 데이터를 앞으로 이동
		if (FreeSize() < iBufferSize)
		{
			::memcpy(&Buffer[0], &Buffer[iReadPos], iDataSize);
			iReadPos = 0;
			iWritePos = iDataSize;
		}
	}
}

// 읽기 커서를 전진시킨다. 처리 완료된 데이터만큼 호출한다.
bool RecvBuffer::OnRead(int32 iNumOfBytes)
{
	if (iNumOfBytes > DataSize())
		return false;

	iReadPos += iNumOfBytes;
	return true;
}

// 쓰기 커서를 전진시킨다. 소켓에서 수신한 바이트만큼 호출한다.
bool RecvBuffer::OnWrite(int32 iNumOfBytes)
{
	if (iNumOfBytes > FreeSize())
		return false;

	iWritePos += iNumOfBytes;
	return true;
}