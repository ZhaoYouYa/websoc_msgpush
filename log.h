#ifndef LOGGER_H
#define LOGGER_H

#include <boost/log/attributes/named_scope.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Log {
enum class LogType { Console, File };

void Init(LogType type, int level, int maxFileSize = 1024,
          int maxBackupIndex = 1);

typedef boost::log::sinks::synchronous_sink<
    boost::log::sinks::text_file_backend>
    file_sink;
static boost::log::sources::severity_logger<boost::log::trivial::severity_level>
    _logger;

};     // namespace Log


#define SLOG_TRACE(logEvent)  BOOST_LOG_FUNCTION(); BOOST_LOG_SEV(Log::_logger, boost::log::trivial::trace) << logEvent;

#define SLOG_DEBUG(logEvent)  BOOST_LOG_FUNCTION(); BOOST_LOG_SEV(Log::_logger, boost::log::trivial::debug) << logEvent;

#define SLOG_INFO(logEvent)   BOOST_LOG_FUNCTION(); BOOST_LOG_SEV(Log::_logger, boost::log::trivial::info) << logEvent;

#define SLOG_WARN(logEvent)   BOOST_LOG_FUNCTION(); BOOST_LOG_SEV(Log::_logger, boost::log::trivial::warning) << logEvent;

#define SLOG_ERROR(logEvent)  BOOST_LOG_FUNCTION(); BOOST_LOG_SEV(Log::_logger, boost::log::trivial::error) << logEvent;

#define SLOG_FATAL(logEvent)  BOOST_LOG_FUNCTION(); BOOST_LOG_SEV(Log::_logger, boost::log::trivial::fatal) << logEvent;
#endif // IDAS_LOGGER_H