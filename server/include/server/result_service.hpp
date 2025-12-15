/*
 * 설명: 결과 저장과 레이팅 반영을 묶어 중복 적용을 방지한다.
 * 버전: v1.0.0
 * 관련 문서: design/protocol/contract.md, design/server/v0.6.0-rating-leaderboard.md
 * 테스트: server/tests/e2e/rating_leaderboard_test.cpp
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "server/rating.hpp"
#include "server/result_repository.hpp"

namespace server {

struct SessionParticipant;

class ResultService {
 public:
  ResultService(std::shared_ptr<ResultRepository> repository, std::shared_ptr<RatingService> rating_service);

  bool FinalizeResult(const MatchResultRecord& record, const std::vector<SessionParticipant>& participants);
  std::size_t Count() const { return repository_->Count(); }
  std::optional<MatchResultRecord> Find(const std::string& session_id) const { return repository_->Find(session_id); }
  std::shared_ptr<RatingService> GetRatingService() { return rating_service_; }

 private:
  std::shared_ptr<ResultRepository> repository_;
  std::shared_ptr<RatingService> rating_service_;
};

}  // namespace server
