#pragma once
void test_ufs(void);
/* After test_ufs: show the end of XNU's persistent console log (patch 0047). */
void ufs_show_log(void);
/* Same, but only lines containing one of the '|'-separated keys, from the whole ring. */
void ufs_show_log_matching(const char *keys);
