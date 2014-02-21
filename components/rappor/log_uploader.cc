// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/rappor/log_uploader.h"

#include "base/metrics/histogram.h"
#include "base/metrics/sparse_histogram.h"
#include "net/base/load_flags.h"
#include "net/url_request/url_fetcher.h"

namespace {

// The delay, in seconds, between uploading when there are queued logs to send.
const int kUnsentLogsIntervalSeconds = 3;

// When uploading metrics to the server fails, we progressively wait longer and
// longer before sending the next log. This backoff process helps reduce load
// on a server that is having issues.
// The following is the multiplier we use to expand that inter-log duration.
const double kBackoffMultiplier = 1.1;

// The maximum backoff multiplier.
const int kMaxBackoffIntervalSeconds = 60 * 60;

// The maximum number of unsent logs we will keep.
// TODO(holte): Limit based on log size instead.
const size_t kMaxQueuedLogs = 10;

enum DiscardReason {
  UPLOAD_SUCCESS,
  UPLOAD_REJECTED,
  QUEUE_OVERFLOW,
  NUM_DISCARD_REASONS
};

}  // namespace

namespace rappor {

LogUploader::LogUploader(const GURL& server_url,
                         const std::string& mime_type,
                         net::URLRequestContextGetter* request_context)
    : server_url_(server_url),
      mime_type_(mime_type),
      request_context_(request_context),
      has_callback_pending_(false),
      upload_interval_(base::TimeDelta::FromSeconds(
          kUnsentLogsIntervalSeconds)) {
}

LogUploader::~LogUploader() {}

void LogUploader::QueueLog(const std::string& log) {
  queued_logs_.push(log);
  if (!IsUploadScheduled() && !has_callback_pending_)
    StartScheduledUpload();
}

bool LogUploader::IsUploadScheduled() const {
  return upload_timer_.IsRunning();
}

void LogUploader::ScheduleNextUpload(base::TimeDelta interval) {
  if (IsUploadScheduled() || has_callback_pending_)
    return;

  upload_timer_.Start(
      FROM_HERE, interval, this, &LogUploader::StartScheduledUpload);
}

void LogUploader::StartScheduledUpload() {
  DCHECK(!has_callback_pending_);
  has_callback_pending_ = true;
  current_fetch_.reset(
      net::URLFetcher::Create(server_url_, net::URLFetcher::POST, this));
  current_fetch_->SetRequestContext(request_context_.get());
  current_fetch_->SetUploadData(mime_type_, queued_logs_.front());

  // We already drop cookies server-side, but we might as well strip them out
  // client-side as well.
  current_fetch_->SetLoadFlags(net::LOAD_DO_NOT_SAVE_COOKIES |
                               net::LOAD_DO_NOT_SEND_COOKIES);
  current_fetch_->Start();
}

// static
base::TimeDelta LogUploader::BackOffUploadInterval(base::TimeDelta interval) {
  DCHECK_GT(kBackoffMultiplier, 1.0);
  interval = base::TimeDelta::FromMicroseconds(static_cast<int64>(
      kBackoffMultiplier * interval.InMicroseconds()));

  base::TimeDelta max_interval =
      base::TimeDelta::FromSeconds(kMaxBackoffIntervalSeconds);
  return interval > max_interval ? max_interval : interval;
}

void LogUploader::OnURLFetchComplete(const net::URLFetcher* source) {
  // We're not allowed to re-use the existing |URLFetcher|s, so free them here.
  // Note however that |source| is aliased to the fetcher, so we should be
  // careful not to delete it too early.
  DCHECK_EQ(current_fetch_.get(), source);
  scoped_ptr<net::URLFetcher> fetch(current_fetch_.Pass());

  int response_code = source->GetResponseCode();

  // Log a histogram to track response success vs. failure rates.
  UMA_HISTOGRAM_SPARSE_SLOWLY("Rappor.UploadResponseCode", response_code);

  bool upload_succeeded = response_code == 200;

  // Determine whether this log should be retransmitted.
  DiscardReason reason = NUM_DISCARD_REASONS;
  if (upload_succeeded) {
    reason = UPLOAD_SUCCESS;
  } else if (response_code == 400) {
    reason = UPLOAD_REJECTED;
  } else if (queued_logs_.size() > kMaxQueuedLogs) {
    reason = QUEUE_OVERFLOW;
  }

  if (reason != NUM_DISCARD_REASONS) {
    UMA_HISTOGRAM_ENUMERATION("Rappor.DiscardReason",
                              reason,
                              NUM_DISCARD_REASONS);
    queued_logs_.pop();
  }

  // Error 400 indicates a problem with the log, not with the server, so
  // don't consider that a sign that the server is in trouble.
  bool server_is_healthy = upload_succeeded || response_code == 400;
  OnUploadFinished(server_is_healthy, !queued_logs_.empty());
}

void LogUploader::OnUploadFinished(bool server_is_healthy,
                                   bool more_logs_remaining) {
  DCHECK(has_callback_pending_);
  has_callback_pending_ = false;
  // If the server is having issues, back off. Otherwise, reset to default.
  if (!server_is_healthy)
    upload_interval_ = BackOffUploadInterval(upload_interval_);
  else
    upload_interval_ = base::TimeDelta::FromSeconds(kUnsentLogsIntervalSeconds);

  if (more_logs_remaining)
    ScheduleNextUpload(upload_interval_);
}

}  // namespace rappor
