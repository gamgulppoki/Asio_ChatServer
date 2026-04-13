#include "SendBuffer.h"
#include "CoreTLS.h"
#include "CoreGlobal.h"

// =============================================
// SendBuffer
// =============================================

SendBuffer::SendBuffer(SharedPtr<SendBufferChunk> Owner, BYTE* Buffer, uint32 iAllocSize)
	: Owner(Owner)
	, Buffer(Buffer)
	, iAllocSize(iAllocSize)
{
}

// 버퍼 시작 포인터를 반환한다.
BYTE* SendBuffer::Data()
{
	return Buffer;
}

// 할당받은 크기를 반환한다.
uint32 SendBuffer::AllocSize() const
{
	return iAllocSize;
}

// Close로 확정된 실제 사용 크기를 반환한다.
uint32 SendBuffer::WriteSize() const
{
	return iWriteSize;
}

// 실제 기록한 크기를 확정하고, Chunk에도 알린다.
void SendBuffer::Close(uint32 iSize)
{
	ASSERT_CRASH(iAllocSize >= iSize)
	iWriteSize = iSize;
	Owner->Close(iSize);
}

// =============================================
// SendBufferChunk
// =============================================

SendBufferChunk::SendBufferChunk()
{
}

// Chunk를 재사용할 때 상태를 초기화한다.
void SendBufferChunk::Reset()
{
	bOpen = false;
	iUsedSize = 0;
}

// 요청 크기만큼 SendBuffer를 잘라서 반환한다.
SendBufferRef SendBufferChunk::Open(uint32 iAllocSize)
{
	ASSERT_CRASH(iAllocSize <= FreeSize())
	ASSERT_CRASH(bOpen == false)

	bOpen = true;

	return std::make_shared<SendBuffer>(shared_from_this(), &Buffer[iUsedSize], iAllocSize);
}

// SendBuffer가 Close될 때 호출. 사용량을 누적한다.
void SendBufferChunk::Close(uint32 iWriteSize)
{
	ASSERT_CRASH(bOpen == true)
	bOpen = false;
	iUsedSize += iWriteSize;
}

// =============================================
// SendBufferManager
// =============================================

// 패킷 크기에 맞는 SendBuffer를 반환한다.
// TLS에 저장된 Chunk를 우선 사용하고, 부족하면 새 Chunk를 가져온다.
SendBufferRef SendBufferManager::Open(uint32 iSize)
{
	if (LSendBufferChunk == nullptr)
	{
		LSendBufferChunk = Pop();
		LSendBufferChunk->Reset();
	}

	ASSERT_CRASH(LSendBufferChunk->IsOpen() == false)

	if (LSendBufferChunk->FreeSize() < iSize)
	{
		LSendBufferChunk = Pop();
		LSendBufferChunk->Reset();
	}

	return LSendBufferChunk->Open(iSize);
}

// 풀에서 Chunk를 꺼낸다. 없으면 새로 만든다.
SendBufferChunkRef SendBufferManager::Pop()
{
	{
		WRITE_LOCK;
		if (ChunkPool.empty() == false)
		{
			SendBufferChunkRef Chunk = ChunkPool.back();
			ChunkPool.pop_back();
			return Chunk;
		}
	}

	return SendBufferChunkRef(new SendBufferChunk(), [this](SendBufferChunk* Buffer) { PushGlobal(Buffer); });
}

// Chunk를 풀에 반납한다.
void SendBufferManager::Push(SendBufferChunkRef Buffer)
{
	WRITE_LOCK;
	ChunkPool.push_back(Buffer);
}

// shared_ptr 커스텀 삭제자. Chunk가 소멸 대신 풀로 돌아간다.
void SendBufferManager::PushGlobal(SendBufferChunk* Buffer)
{
	Push(SendBufferChunkRef(Buffer, [this](SendBufferChunk* Buffer) { PushGlobal(Buffer); }));
}