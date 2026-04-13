#pragma once

// =============================================
// 전역 싱글톤 선언
// 프로젝트 전체에서 하나만 존재하는 매니저 객체들
// =============================================

extern class ThreadManager* GThreadManager;
extern class SendBufferManager* GSendBufferManager;
extern class GlobalQueue* GGlobalQueue;