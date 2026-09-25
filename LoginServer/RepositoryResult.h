#pragma once

// Repository 결과를 "조회 결과 없음"과 "DB 자체 오류"로 구분한다.
// 상위 계층은 NotFound를 정상적인 비즈니스 결과로 처리할 수 있다.
enum class RepositoryStatus
{
    Success,
    NotFound,
    DatabaseError
};