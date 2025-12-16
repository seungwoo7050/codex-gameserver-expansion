/*
 * 설명: 세션 결과를 저장하고 레이팅 반영을 단일 트랜잭션으로 수행한다.
 * 버전: v1.1.0
 * 관련 문서: design/protocol/contract.md, design/server/v0.6.0-rating-leaderboard.md
 * 테스트: server/tests/e2e/rating_leaderboard_test.cpp
 */
#include "server/result_service.hpp"

#include "server/session_manager.hpp"

namespace server {

ResultService::ResultService(std::shared_ptr<MariaDbClient> db_client, std::shared_ptr<ResultRepository> repository,
                             std::shared_ptr<RatingService> rating_service)
    : db_client_(std::move(db_client)), repository_(std::move(repository)), rating_service_(std::move(rating_service)) {}

bool ResultService::FinalizeResult(const MatchResultRecord& record, const std::vector<SessionParticipant>& participants) {
  return db_client_->ExecuteTransactionWithRetry([&](MYSQL* conn) {
    bool inserted = repository_->InsertMatchResult(conn, record);
    if (!inserted) {
      return true;
    }

    for (const auto& participant : participants) {
      rating_service_->EnsureUserInTx(conn, participant.user_id, participant.username);
    }
    rating_service_->EnsureUserInTx(conn, record.user1_id, "");
    rating_service_->EnsureUserInTx(conn, record.user2_id, "");

    int loser_id = record.winner_user_id == record.user1_id ? record.user2_id : record.user1_id;
    bool winner_guard = repository_->InsertRatingGuard(conn, record.session_id, record.winner_user_id);
    bool loser_guard = repository_->InsertRatingGuard(conn, record.session_id, loser_id);
    if (!(winner_guard && loser_guard)) {
      return true;
    }

    rating_service_->ApplyMatchResultInTx(conn, record.winner_user_id, loser_id);
    return true;
  });
}

}  // namespace server
