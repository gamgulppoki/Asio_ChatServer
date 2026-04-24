#pragma once

#include "Types.h"
#include <mutex>

class SendBufferChunk;

// =============================================
// SendBuffer: 패킷 하나에 대응하는 버퍼
// Chunk의 일부분을 가리키는 포인터만 가진다.
// =============================================
class SendBuffer
{
public:
	SendBuffer(SharedPtr<SendBufferChunk> Owner, BYTE* Buffer, uint32 iAllocSize);

	BYTE* Data();
	uint32 AllocSize() const;
	uint32 WriteSize() const;
	void Close(uint32 iSize);

private:
	SharedPtr<SendBufferChunk> Owner;
	BYTE* Buffer;
	uint32 iAllocSize = 0;
	uint32 iWriteSize = 0;
};

using SendBufferRef = SharedPtr<SendBuffer>;

// =============================================
// SendBufferChunk: 큰 메모리 덩어리 (6000바이트)
// SendBuffer 여러 개가 이 안에서 잘라 쓴다.
// =============================================
class SendBufferChunk : public std::enable_shared_from_this<SendBufferChunk>
{
	static constexpr uint32 CHUNK_SIZE = 6000;

public:
	SendBufferChunk();

	void Reset();
	SendBufferRef Open(uint32 iAllocSize);
	void Close(uint32 iWriteSize);

	bool IsOpen() const { return bOpen; }
	uint32 FreeSize() const { return CHUNK_SIZE - iUsedSize; }

private:
	Array<BYTE, CHUNK_SIZE> Buffer = {};
	bool bOpen = false;
	uint32 iUsedSize = 0;
};

using SendBufferChunkRef = SharedPtr<SendBufferChunk>;

// =============================================
// SendBufferManager: Chunk를 생성/관리하는 전역 매니저
// =============================================
class SendBufferManager
{
public:
	SendBufferRef Open(uint32 iSize);

private:
	SendBufferChunkRef Pop();
	void Push(SendBufferChunkRef Buffer);
	void PushGlobal(SendBufferChunk* Buffer);

	std::mutex Mutex_;
	Vector<SendBufferChunkRef> ChunkPool;
};