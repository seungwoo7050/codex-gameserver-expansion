CREATE TABLE IF NOT EXISTS match_results (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  match_id VARCHAR(64) NOT NULL,
  user1_id INT NOT NULL,
  user2_id INT NOT NULL,
  winner_user_id INT NOT NULL,
  tick_count INT NOT NULL,
  ended_at DATETIME(6) NOT NULL,
  snapshot JSON NOT NULL,
  PRIMARY KEY (id),
  UNIQUE KEY uk_match_id (match_id)
);

CREATE TABLE IF NOT EXISTS ratings (
  user_id INT NOT NULL,
  username VARCHAR(255) NOT NULL,
  rating INT NOT NULL,
  wins INT NOT NULL,
  losses INT NOT NULL,
  updated_at DATETIME(6) NOT NULL,
  PRIMARY KEY (user_id),
  KEY idx_rating_desc (rating DESC, user_id ASC),
  KEY idx_username (username)
);

CREATE TABLE IF NOT EXISTS rating_applies (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  match_id VARCHAR(64) NOT NULL,
  user_id INT NOT NULL,
  applied_at DATETIME(6) NOT NULL,
  PRIMARY KEY (id),
  UNIQUE KEY uk_match_user (match_id, user_id),
  KEY idx_user (user_id)
);
