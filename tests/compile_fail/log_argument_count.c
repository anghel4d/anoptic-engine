#include <ano/log.h>

void invalid_log_argument_count(void)
{
    ano::log(ano::Info, "pair: %d %s", 7);
}
