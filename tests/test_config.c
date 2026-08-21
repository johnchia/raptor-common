#include "greatest.h"
#include "rss_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CFG_PATH "/tmp/rss_test_config.ini"

static void cleanup(void)
{
	unlink(CFG_PATH);
}

/* Helper: write INI content, load, return config (caller frees) */
static rss_config_t *load_ini(const char *content)
{
	int ret = rss_write_file_atomic(CFG_PATH, content, (int)strlen(content));
	if (ret != 0)
		return NULL;
	return rss_config_load(CFG_PATH);
}

TEST config_load_basic(void)
{
	rss_config_t *cfg = load_ini("[network]\nport = 554\nhost = 0.0.0.0\n");
	ASSERT(cfg);
	ASSERT_STR_EQ("554", rss_config_get_str(cfg, "network", "port", ""));
	ASSERT_STR_EQ("0.0.0.0", rss_config_get_str(cfg, "network", "host", ""));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_get_str_default(void)
{
	rss_config_t *cfg = load_ini("[s]\nkey = val\n");
	ASSERT(cfg);
	/* Missing key */
	ASSERT_STR_EQ("default", rss_config_get_str(cfg, "s", "nokey", "default"));
	/* Missing section */
	ASSERT_STR_EQ("default", rss_config_get_str(cfg, "nosec", "key", "default"));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_get_int(void)
{
	rss_config_t *cfg = load_ini("[s]\nport = 8554\nbad = abc\nneg = -10\n");
	ASSERT(cfg);
	ASSERT_EQ(8554, rss_config_get_int(cfg, "s", "port", 0));
	ASSERT_EQ(99, rss_config_get_int(cfg, "s", "bad", 99));
	ASSERT_EQ(-10, rss_config_get_int(cfg, "s", "neg", 0));
	/* Missing key */
	ASSERT_EQ(42, rss_config_get_int(cfg, "s", "nokey", 42));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_get_bool(void)
{
	rss_config_t *cfg = load_ini(
		"[b]\n"
		"a = true\nb = false\nc = yes\nd = no\n"
		"e = 1\nf = 0\ng = on\nh = off\n"
		"i = TRUE\nj = False\nk = YeS\n");
	ASSERT(cfg);
	ASSERT(rss_config_get_bool(cfg, "b", "a", false));
	ASSERT_FALSE(rss_config_get_bool(cfg, "b", "b", true));
	ASSERT(rss_config_get_bool(cfg, "b", "c", false));
	ASSERT_FALSE(rss_config_get_bool(cfg, "b", "d", true));
	ASSERT(rss_config_get_bool(cfg, "b", "e", false));
	ASSERT_FALSE(rss_config_get_bool(cfg, "b", "f", true));
	ASSERT(rss_config_get_bool(cfg, "b", "g", false));
	ASSERT_FALSE(rss_config_get_bool(cfg, "b", "h", true));
	/* Case insensitive */
	ASSERT(rss_config_get_bool(cfg, "b", "i", false));
	ASSERT_FALSE(rss_config_get_bool(cfg, "b", "j", true));
	ASSERT(rss_config_get_bool(cfg, "b", "k", false));
	/* Missing → default */
	ASSERT(rss_config_get_bool(cfg, "b", "nope", true));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_comments(void)
{
	rss_config_t *cfg = load_ini(
		"# full line comment\n"
		"; also a comment\n"
		"[s]\n"
		"key = value # inline comment\n"
		"plain = nohash\n");
	ASSERT(cfg);
	ASSERT_STR_EQ("value", rss_config_get_str(cfg, "s", "key", ""));
	ASSERT_STR_EQ("nohash", rss_config_get_str(cfg, "s", "plain", ""));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_global_section(void)
{
	rss_config_t *cfg = load_ini("global_key = global_val\n[s]\nkey = val\n");
	ASSERT(cfg);
	ASSERT_STR_EQ("global_val", rss_config_get_str(cfg, "", "global_key", ""));
	ASSERT_STR_EQ("val", rss_config_get_str(cfg, "s", "key", ""));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_case_insensitive(void)
{
	rss_config_t *cfg = load_ini("[Network]\nPort = 554\n");
	ASSERT(cfg);
	/* Section case */
	ASSERT_STR_EQ("554", rss_config_get_str(cfg, "network", "Port", ""));
	ASSERT_STR_EQ("554", rss_config_get_str(cfg, "NETWORK", "Port", ""));
	/* Key case */
	ASSERT_STR_EQ("554", rss_config_get_str(cfg, "Network", "port", ""));
	ASSERT_STR_EQ("554", rss_config_get_str(cfg, "Network", "PORT", ""));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_set_str(void)
{
	rss_config_t *cfg = load_ini("[s]\nkey = old\n");
	ASSERT(cfg);
	/* Overwrite existing */
	rss_config_set_str(cfg, "s", "key", "new");
	ASSERT_STR_EQ("new", rss_config_get_str(cfg, "s", "key", ""));
	/* Create new key */
	rss_config_set_str(cfg, "s", "added", "fresh");
	ASSERT_STR_EQ("fresh", rss_config_get_str(cfg, "s", "added", ""));
	/* Create new section + key */
	rss_config_set_str(cfg, "new_sec", "k", "v");
	ASSERT_STR_EQ("v", rss_config_get_str(cfg, "new_sec", "k", ""));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_set_int(void)
{
	rss_config_t *cfg = load_ini("[s]\n");
	ASSERT(cfg);
	rss_config_set_int(cfg, "s", "port", 8080);
	ASSERT_EQ(8080, rss_config_get_int(cfg, "s", "port", 0));
	rss_config_set_int(cfg, "s", "neg", -42);
	ASSERT_EQ(-42, rss_config_get_int(cfg, "s", "neg", 0));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_save_roundtrip(void)
{
	rss_config_t *cfg = load_ini("[a]\nk1 = v1\nk2 = v2\n[b]\nk3 = v3\n");
	ASSERT(cfg);
	rss_config_set_str(cfg, "a", "k4", "v4");

	const char *save_path = "/tmp/rss_test_config_save.ini";
	ASSERT_EQ(0, rss_config_save(cfg, save_path));
	rss_config_free(cfg);

	/* Reload and verify */
	rss_config_t *cfg2 = rss_config_load(save_path);
	ASSERT(cfg2);
	ASSERT_STR_EQ("v1", rss_config_get_str(cfg2, "a", "k1", ""));
	ASSERT_STR_EQ("v2", rss_config_get_str(cfg2, "a", "k2", ""));
	ASSERT_STR_EQ("v4", rss_config_get_str(cfg2, "a", "k4", ""));
	ASSERT_STR_EQ("v3", rss_config_get_str(cfg2, "b", "k3", ""));
	rss_config_free(cfg2);
	unlink(save_path);
	cleanup();
	PASS();
}

struct foreach_ctx {
	int count;
	char keys[16][64];
	char vals[16][256];
};

static void foreach_cb(const char *key, const char *value, void *userdata)
{
	struct foreach_ctx *ctx = userdata;
	if (ctx->count < 16) {
		rss_strlcpy(ctx->keys[ctx->count], key, 64);
		rss_strlcpy(ctx->vals[ctx->count], value, 256);
		ctx->count++;
	}
}

TEST config_foreach(void)
{
	rss_config_t *cfg = load_ini("[s]\na = 1\nb = 2\nc = 3\n");
	ASSERT(cfg);
	struct foreach_ctx ctx = {0};
	int n = rss_config_foreach(cfg, "s", foreach_cb, &ctx);
	ASSERT_EQ(3, n);
	ASSERT_EQ(3, ctx.count);
	/* Verify all keys present (order may be reversed due to linked list) */
	int found = 0;
	for (int i = 0; i < ctx.count; i++) {
		if (strcmp(ctx.keys[i], "a") == 0) { ASSERT_STR_EQ("1", ctx.vals[i]); found++; }
		if (strcmp(ctx.keys[i], "b") == 0) { ASSERT_STR_EQ("2", ctx.vals[i]); found++; }
		if (strcmp(ctx.keys[i], "c") == 0) { ASSERT_STR_EQ("3", ctx.vals[i]); found++; }
	}
	ASSERT_EQ(3, found);
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_empty_file(void)
{
	rss_config_t *cfg = load_ini("");
	ASSERT(cfg);
	ASSERT_STR_EQ("default", rss_config_get_str(cfg, "s", "k", "default"));
	ASSERT_EQ(99, rss_config_get_int(cfg, "s", "k", 99));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_long_value(void)
{
	/* MAX_VAL is 256, so 255 chars should fit */
	char ini[1024];
	char long_val[260];
	memset(long_val, 'X', 259);
	long_val[259] = '\0';
	snprintf(ini, sizeof(ini), "[s]\nkey = %s\n", long_val);

	rss_config_t *cfg = load_ini(ini);
	ASSERT(cfg);
	const char *v = rss_config_get_str(cfg, "s", "key", "");
	/* Value should be truncated to MAX_VAL-1 = 255 chars */
	ASSERT(strlen(v) == 255);
	rss_config_free(cfg);
	cleanup();
	PASS();
}

/* A line over MAX_LINE-1 chars must be rejected whole: no truncated
 * value accepted from the head, no phantom key parsed from the tail. */
TEST config_line_too_long_ignored(void)
{
	char longline[700];
	memset(longline, 'A', sizeof(longline));
	/* Head looks like a valid entry; tail contains what would misparse
	 * as a second entry if the line were split at the buffer boundary. */
	memcpy(longline, "toolong = ", 10);
	memcpy(longline + 600, "\nphantom = tail\n", 16);
	longline[616] = '\0';

	char ini[1024];
	snprintf(ini, sizeof(ini), "[s]\nbefore = 1\n%safter = 2\n", longline);

	rss_config_t *cfg = load_ini(ini);
	ASSERT(cfg);
	ASSERT_STR_EQ("1", rss_config_get_str(cfg, "s", "before", ""));
	ASSERT_STR_EQ("2", rss_config_get_str(cfg, "s", "after", ""));
	/* The over-long entry is dropped entirely, not truncated-and-kept */
	ASSERT_EQ(NULL, rss_config_get_str(cfg, "s", "toolong", NULL));
	/* "phantom = tail" was inside the over-long line's spillover and is
	 * legitimate input on the next physical line -- it must parse. */
	ASSERT_STR_EQ("tail", rss_config_get_str(cfg, "s", "phantom", ""));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

/* A final line that exactly fills the buffer with no trailing newline
 * is a complete line, not an over-long one -- it must parse. */
TEST config_line_exact_fit_no_newline(void)
{
	/* MAX_LINE is 512; build content whose last line is exactly 511
	 * chars with no newline: "k = " + 507 X's. */
	char last[512];
	memcpy(last, "k = ", 4);
	memset(last + 4, 'X', 507);
	last[511] = '\0';

	char ini[1024];
	snprintf(ini, sizeof(ini), "[s]\n%s", last);

	rss_config_t *cfg = load_ini(ini);
	ASSERT(cfg);
	const char *v = rss_config_get_str(cfg, "s", "k", NULL);
	ASSERT(v);
	/* Value capped by MAX_VAL like any other long value */
	ASSERT_EQ(255, (int)strlen(v));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

TEST config_duplicate_key(void)
{
	rss_config_t *cfg = load_ini("[s]\nkey = first\nkey = second\n");
	ASSERT(cfg);
	ASSERT_STR_EQ("second", rss_config_get_str(cfg, "s", "key", ""));
	rss_config_free(cfg);
	cleanup();
	PASS();
}

/* Simulate two daemons saving to the same file — only dirty keys merge */
TEST config_save_dirty_merge(void)
{
	const char *path = "/tmp/rss_test_config_merge.ini";

	/* Write initial shared config */
	rss_config_t *init = load_ini("[audio]\ncodec = aac\nvolume = 80\n[stream0]\nfps = 25\n");
	ASSERT(init);
	rss_config_set_str(init, "audio", "codec", "aac"); /* mark all dirty for initial write */
	rss_config_set_str(init, "audio", "volume", "80");
	rss_config_set_str(init, "stream0", "fps", "25");
	ASSERT_EQ(0, rss_config_save(init, path));
	rss_config_free(init);

	/* Daemon A (RVD) loads config, changes stream0/fps */
	rss_config_t *daemon_a = rss_config_load(path);
	ASSERT(daemon_a);
	rss_config_set_int(daemon_a, "stream0", "fps", 15);
	/* daemon_a also has [audio] from file load — but NOT dirty */

	/* Daemon B (RAD) loads config, changes audio/volume */
	rss_config_t *daemon_b = rss_config_load(path);
	ASSERT(daemon_b);
	rss_config_set_int(daemon_b, "audio", "volume", 50);
	/* daemon_b also has [stream0] from file load — but NOT dirty */

	/* Both save sequentially (like raptorctl config save) */
	ASSERT_EQ(0, rss_config_save(daemon_a, path));
	ASSERT_EQ(0, rss_config_save(daemon_b, path));
	rss_config_free(daemon_a);
	rss_config_free(daemon_b);

	/* Verify: both changes survived, neither clobbered the other */
	rss_config_t *result = rss_config_load(path);
	ASSERT(result);
	ASSERT_STR_EQ("aac", rss_config_get_str(result, "audio", "codec", ""));
	ASSERT_EQ(50, rss_config_get_int(result, "audio", "volume", 0));
	ASSERT_EQ(15, rss_config_get_int(result, "stream0", "fps", 0));
	rss_config_free(result);
	unlink(path);
	cleanup();
	PASS();
}

/* Defaults populated by get_* must NOT be dirty */
TEST config_save_defaults_not_dirty(void)
{
	const char *path = "/tmp/rss_test_config_defaults.ini";

	/* Write minimal config */
	rss_config_t *init = load_ini("[audio]\ncodec = opus\n");
	ASSERT(init);
	rss_config_set_str(init, "audio", "codec", "opus");
	ASSERT_EQ(0, rss_config_save(init, path));
	rss_config_free(init);

	/* Daemon loads config, reads a key with default (populates it) */
	rss_config_t *daemon = rss_config_load(path);
	ASSERT(daemon);
	int vol = rss_config_get_int(daemon, "audio", "volume", 80);
	ASSERT_EQ(80, vol);
	/* volume=80 is now in memory but NOT dirty */

	/* Another process writes volume=50 to the file */
	rss_config_t *other = rss_config_load(path);
	ASSERT(other);
	rss_config_set_int(other, "audio", "volume", 50);
	ASSERT_EQ(0, rss_config_save(other, path));
	rss_config_free(other);

	/* Original daemon saves — should NOT overwrite volume=50 with default 80 */
	ASSERT_EQ(0, rss_config_save(daemon, path));
	rss_config_free(daemon);

	rss_config_t *result = rss_config_load(path);
	ASSERT(result);
	ASSERT_EQ(50, rss_config_get_int(result, "audio", "volume", 0));
	ASSERT_STR_EQ("opus", rss_config_get_str(result, "audio", "codec", ""));
	rss_config_free(result);
	unlink(path);
	cleanup();
	PASS();
}

/*
 * Multi-daemon config save stress test.
 *
 * Simulates the real raptor daemon lifecycle: 7 daemons share one config
 * file, each owns different sections, runtime changes are made via
 * rss_config_set_*, and raptorctl broadcasts config-save to all daemons
 * sequentially.  We run 100 cycles and verify no data loss or corruption.
 */

#define STRESS_PATH "/tmp/rss_test_config_stress.ini"
#define NUM_DAEMONS 7
#define NUM_CYCLES 100

/* Daemon section ownership — mirrors real raptor */
/* Daemon names for documentation — matches daemon_sections[] order */
/* static const char *daemon_names[] = {"rvd","rsd","rad","rod","rhd","ric","rwd"}; */

/* Each daemon's owned sections */
static const char *daemon_sections[][4] = {
	{"stream0", "stream1", "image", NULL},   /* rvd */
	{"rtsp", NULL, NULL, NULL},              /* rsd */
	{"audio", NULL, NULL, NULL},             /* rad */
	{"osd", NULL, NULL, NULL},               /* rod */
	{"http", NULL, NULL, NULL},              /* rhd */
	{"ircut", NULL, NULL, NULL},             /* ric */
	{"webrtc", "webtorrent", NULL, NULL},    /* rwd */
};

/* Simulated keys per section */
static const char *section_keys[][4] = {
	{"fps", "bitrate", "gop", NULL},            /* stream0 */
	{"fps", "bitrate", NULL, NULL},             /* stream1 */
	{"brightness", "contrast", NULL, NULL},     /* image */
	{"port", "max_clients", NULL, NULL},        /* rtsp */
	{"codec", "volume", "gain", NULL},          /* audio */
	{"text_string", "font_color", NULL, NULL},  /* osd */
	{"port", "max_clients", NULL, NULL},        /* http */
	{"mode", NULL, NULL, NULL},                 /* ircut */
	{"udp_port", "http_port", NULL, NULL},      /* webrtc */
	{"enabled", NULL, NULL, NULL},              /* webtorrent */
};

/* Map section name → key list index */
static const char *all_sections[] = {
	"stream0", "stream1", "image", "rtsp", "audio",
	"osd", "http", "ircut", "webrtc", "webtorrent"
};

static int find_section_idx(const char *name)
{
	for (int i = 0; i < 10; i++) {
		if (strcmp(all_sections[i], name) == 0)
			return i;
	}
	return -1;
}

TEST config_save_stress_multi_daemon(void)
{
	/* Write initial config with one key per section */
	const char *initial =
		"[stream0]\nfps = 25\nbitrate = 3000000\ngop = 50\n"
		"[stream1]\nfps = 25\nbitrate = 1000000\n"
		"[image]\nbrightness = 128\ncontrast = 128\n"
		"[rtsp]\nport = 554\nmax_clients = 4\n"
		"[audio]\ncodec = aac\nvolume = 80\ngain = 25\n"
		"[osd]\ntext_string = Camera\nfont_color = 0xFFFFFFFF\n"
		"[http]\nport = 8080\nmax_clients = 4\n"
		"[ircut]\nmode = auto\n"
		"[webrtc]\nudp_port = 8443\nhttp_port = 8554\n"
		"[webtorrent]\nenabled = false\n"
		"[log]\nlevel = info\n";

	rss_config_t *seed = load_ini(initial);
	ASSERT(seed);
	/* Mark all dirty for initial write */
	for (int i = 0; i < 10; i++) {
		for (int k = 0; section_keys[i][k]; k++)
			rss_config_set_str(seed, all_sections[i], section_keys[i][k],
					   rss_config_get_str(seed, all_sections[i],
							      section_keys[i][k], ""));
	}
	rss_config_set_str(seed, "log", "level", "info");
	ASSERT_EQ(0, rss_config_save(seed, STRESS_PATH));
	rss_config_free(seed);

	/*
	 * Track expected values. Each cycle, one or more daemons change a
	 * value, then all daemons save sequentially. After each cycle we
	 * reload and verify every key matches expectations.
	 */
	char expected[10][4][64]; /* [section_idx][key_idx][value] */

	/* Initialize expected from initial config */
	const char *init_vals[10][4] = {
		{"25", "3000000", "50", NULL},       /* stream0 */
		{"25", "1000000", NULL, NULL},       /* stream1 */
		{"128", "128", NULL, NULL},          /* image */
		{"554", "4", NULL, NULL},            /* rtsp */
		{"aac", "80", "25", NULL},           /* audio */
		{"Camera", "0xFFFFFFFF", NULL, NULL},/* osd */
		{"8080", "4", NULL, NULL},           /* http */
		{"auto", NULL, NULL, NULL},          /* ircut */
		{"8443", "8554", NULL, NULL},        /* webrtc */
		{"false", NULL, NULL, NULL},         /* webtorrent */
	};

	for (int s = 0; s < 10; s++)
		for (int k = 0; k < 4 && init_vals[s][k]; k++)
			snprintf(expected[s][k], 64, "%s", init_vals[s][k]);

	for (int cycle = 0; cycle < NUM_CYCLES; cycle++) {
		/* Load config for each daemon (simulates daemon startup or
		 * long-running daemon that loaded at boot) */
		rss_config_t *daemons[NUM_DAEMONS];
		for (int d = 0; d < NUM_DAEMONS; d++) {
			daemons[d] = rss_config_load(STRESS_PATH);
			ASSERT(daemons[d]);

			/* Simulate each daemon reading defaults for its sections
			 * (populates in-memory config, must NOT be dirty) */
			for (int si = 0; daemon_sections[d][si]; si++) {
				int idx = find_section_idx(daemon_sections[d][si]);
				if (idx < 0) continue;
				for (int k = 0; section_keys[idx][k]; k++)
					(void)rss_config_get_str(daemons[d],
						all_sections[idx], section_keys[idx][k], "default");
			}

			/* Also read sections owned by OTHER daemons (simulates
			 * shared config load — all sections are in memory) */
			(void)rss_config_get_str(daemons[d], "log", "level", "info");
		}

		/* Pick which daemons modify values this cycle.
		 * Rotate: each cycle, 1-3 daemons make changes. */
		int modifiers[] = {
			cycle % NUM_DAEMONS,
			(cycle * 3 + 1) % NUM_DAEMONS,
			(cycle * 7 + 2) % NUM_DAEMONS
		};
		int nmod = 1 + (cycle % 3); /* 1, 2, or 3 daemons modify */

		for (int m = 0; m < nmod; m++) {
			int d = modifiers[m];
			/* Modify the first key of the daemon's first section */
			const char *sec = daemon_sections[d][0];
			if (!sec) continue;
			int idx = find_section_idx(sec);
			if (idx < 0 || !section_keys[idx][0]) continue;

			char val[64];
			snprintf(val, sizeof(val), "%d", cycle * 100 + d);
			rss_config_set_str(daemons[d], sec, section_keys[idx][0], val);
			snprintf(expected[idx][0], 64, "%s", val);
		}

		/* All daemons save sequentially (like raptorctl config save) */
		for (int d = 0; d < NUM_DAEMONS; d++) {
			ASSERT_EQ(0, rss_config_save(daemons[d], STRESS_PATH));
			rss_config_free(daemons[d]);
		}

		/* Verify: reload file and check every expected key */
		rss_config_t *verify = rss_config_load(STRESS_PATH);
		ASSERT(verify);

		for (int s = 0; s < 10; s++) {
			for (int k = 0; section_keys[s][k]; k++) {
				const char *got = rss_config_get_str(verify,
					all_sections[s], section_keys[s][k], NULL);
				ASSERTm("key missing after save cycle",
					got != NULL);
				if (strcmp(got, expected[s][k]) != 0) {
					fprintf(stderr,
						"MISMATCH cycle %d: [%s] %s = '%s' (expected '%s')\n",
						cycle, all_sections[s], section_keys[s][k],
						got, expected[s][k]);
					FAILm("value mismatch after save cycle");
				}
			}
		}

		/* Also verify [log] section wasn't clobbered */
		ASSERT_STR_EQ("info", rss_config_get_str(verify, "log", "level", ""));
		rss_config_free(verify);
	}

	unlink(STRESS_PATH);
	cleanup();
	PASS();
}

/* Rapid set-then-save on the same key — value must always reflect latest */
TEST config_save_rapid_overwrite(void)
{
	const char *path = "/tmp/rss_test_config_rapid.ini";

	rss_config_t *init = load_ini("[stream0]\nfps = 25\n");
	ASSERT(init);
	rss_config_set_str(init, "stream0", "fps", "25");
	ASSERT_EQ(0, rss_config_save(init, path));
	rss_config_free(init);

	for (int i = 0; i < 100; i++) {
		rss_config_t *cfg = rss_config_load(path);
		ASSERT(cfg);

		char val[16];
		snprintf(val, sizeof(val), "%d", i + 1);
		rss_config_set_str(cfg, "stream0", "fps", val);
		ASSERT_EQ(0, rss_config_save(cfg, path));
		rss_config_free(cfg);

		/* Immediately reload and verify */
		rss_config_t *check = rss_config_load(path);
		ASSERT(check);
		ASSERT_EQ(i + 1, rss_config_get_int(check, "stream0", "fps", -1));
		rss_config_free(check);
	}

	unlink(path);
	cleanup();
	PASS();
}

/* New sections created by set_str must survive cross-daemon saves */
TEST config_save_new_section_survives(void)
{
	const char *path = "/tmp/rss_test_config_newsec.ini";

	/* Start with minimal config — no [stream0] */
	rss_config_t *init = load_ini("[audio]\ncodec = aac\n");
	ASSERT(init);
	rss_config_set_str(init, "audio", "codec", "aac");
	ASSERT_EQ(0, rss_config_save(init, path));
	rss_config_free(init);

	for (int i = 0; i < 50; i++) {
		/* Daemon A creates a new section with a new key */
		rss_config_t *da = rss_config_load(path);
		ASSERT(da);
		char sec[32], val[16];
		snprintf(sec, sizeof(sec), "dynamic%d", i);
		snprintf(val, sizeof(val), "%d", i * 10);
		rss_config_set_str(da, sec, "value", val);
		ASSERT_EQ(0, rss_config_save(da, path));
		rss_config_free(da);

		/* Daemon B saves with no changes — must not clobber new section */
		rss_config_t *db = rss_config_load(path);
		ASSERT(db);
		(void)rss_config_get_str(db, "audio", "codec", "aac");
		ASSERT_EQ(0, rss_config_save(db, path));
		rss_config_free(db);

		/* Verify new section survived */
		rss_config_t *check = rss_config_load(path);
		ASSERT(check);
		ASSERT_EQ(i * 10, rss_config_get_int(check, sec, "value", -1));
		ASSERT_STR_EQ("aac", rss_config_get_str(check, "audio", "codec", ""));
		rss_config_free(check);
	}

	/* Verify ALL dynamic sections survived */
	rss_config_t *final = rss_config_load(path);
	ASSERT(final);
	for (int i = 0; i < 50; i++) {
		char sec[32];
		snprintf(sec, sizeof(sec), "dynamic%d", i);
		ASSERT_EQ(i * 10, rss_config_get_int(final, sec, "value", -1));
	}
	ASSERT_STR_EQ("aac", rss_config_get_str(final, "audio", "codec", ""));
	rss_config_free(final);

	unlink(path);
	cleanup();
	PASS();
}

/* Daemon with no dirty keys must not corrupt the file */
TEST config_save_no_dirty_noop(void)
{
	const char *path = "/tmp/rss_test_config_noop.ini";

	rss_config_t *init = load_ini(
		"[stream0]\nfps = 25\nbitrate = 3000000\n"
		"[audio]\ncodec = aac\nvolume = 80\n");
	ASSERT(init);
	rss_config_set_str(init, "stream0", "fps", "25");
	rss_config_set_str(init, "stream0", "bitrate", "3000000");
	rss_config_set_str(init, "audio", "codec", "aac");
	rss_config_set_str(init, "audio", "volume", "80");
	ASSERT_EQ(0, rss_config_save(init, path));
	rss_config_free(init);

	/* 100 cycles of load→read defaults→save with NO set_str */
	for (int i = 0; i < 100; i++) {
		rss_config_t *cfg = rss_config_load(path);
		ASSERT(cfg);
		/* Read lots of defaults — none should be dirty */
		(void)rss_config_get_int(cfg, "stream0", "gop", 50);
		(void)rss_config_get_bool(cfg, "stream0", "enabled", true);
		(void)rss_config_get_str(cfg, "audio", "sample_rate", "16000");
		(void)rss_config_get_int(cfg, "newdaemon", "newkey", 999);
		ASSERT_EQ(0, rss_config_save(cfg, path));
		rss_config_free(cfg);
	}

	/* File should still have only the original 4 keys */
	rss_config_t *final = rss_config_load(path);
	ASSERT(final);
	ASSERT_EQ(25, rss_config_get_int(final, "stream0", "fps", -1));
	ASSERT_EQ(3000000, rss_config_get_int(final, "stream0", "bitrate", -1));
	ASSERT_STR_EQ("aac", rss_config_get_str(final, "audio", "codec", ""));
	ASSERT_EQ(80, rss_config_get_int(final, "audio", "volume", -1));

	/* Defaults must NOT have leaked to disk */
	/* Reload fresh (no default population) and check raw */
	rss_config_free(final);
	final = rss_config_load(path);
	ASSERT(final);
	const char *gop = rss_config_get_str(final, "stream0", "gop", NULL);
	ASSERTm("default 'gop' leaked to disk", gop == NULL);
	const char *sr = rss_config_get_str(final, "audio", "sample_rate", NULL);
	ASSERTm("default 'sample_rate' leaked to disk", sr == NULL);
	const char *nk = rss_config_get_str(final, "newdaemon", "newkey", NULL);
	ASSERTm("default 'newkey' leaked to disk", nk == NULL);
	rss_config_free(final);

	unlink(path);
	cleanup();
	PASS();
}

/* has_dirty: false after load, true after a runtime set, false again
 * once saved; reads that populate defaults never count */
TEST config_has_dirty_lifecycle(void)
{
	const char *path = "/tmp/rss_test_config_dirty.ini";
	const char *ini = "[s]\nk = v\n";
	ASSERT_EQ(0, rss_write_file_atomic(path, ini, (int)strlen(ini)));
	rss_config_t *cfg = rss_config_load(path);
	ASSERT(cfg);
	ASSERT_EQ(false, rss_config_has_dirty(cfg));
	(void)rss_config_get_int(cfg, "s", "missing", 42);
	(void)rss_config_get_str(cfg, "s", "alsomissing", "d");
	ASSERT_EQ(false, rss_config_has_dirty(cfg));
	rss_config_set_str(cfg, "s", "k", "w");
	ASSERT_EQ(true, rss_config_has_dirty(cfg));
	ASSERT_EQ(0, rss_config_save(cfg, path));
	ASSERT_EQ(false, rss_config_has_dirty(cfg));
	rss_config_free(cfg);
	ASSERT_EQ(false, rss_config_has_dirty(NULL));
	unlink(path);
	PASS();
}

/* ================================================================
 * Comment-preserving surgical save
 * ================================================================ */

#define TPL_PATH "/tmp/rss_test_config_tpl.ini"

static const char *tpl_ini = "# Raptor Streaming System configuration\n"
			     "# See raptor-docs/23-rss-config.md for the full reference\n"
			     "\n"
			     "[sensor]\n"
			     "# All values auto-detected if omitted\n"
			     "# name = gc4653\n"
			     "\n"
			     "[stream0]\n"
			     "fps = 25    # frames per second\n"
			     "enabled = true\n"
			     "# gop = 50\n"
			     "\n"
			     "[audio]\n"
			     "enabled = true\n"
			     "codec = aac\n";

static rss_config_t *load_tpl(void)
{
	if (rss_write_file_atomic(TPL_PATH, tpl_ini, (int)strlen(tpl_ini)) != 0)
		return NULL;
	return rss_config_load(TPL_PATH);
}

/* A save with nothing dirty must not rewrite the file at all */
TEST config_save_clean_leaves_file_untouched(void)
{
	rss_config_t *cfg = load_tpl();
	ASSERT(cfg);

	struct stat before;
	ASSERT_EQ(0, stat(TPL_PATH, &before));

	/* Reads auto-populate defaults; none of this is dirty */
	(void)rss_config_get_int(cfg, "stream0", "fps", 25);
	(void)rss_config_get_int(cfg, "stream0", "gop", 50);
	(void)rss_config_get_bool(cfg, "audio", "enabled", true);
	(void)rss_config_get_str(cfg, "sensor", "name", "auto");

	ASSERT_EQ(0, rss_config_save(cfg, TPL_PATH));
	rss_config_free(cfg);

	struct stat after;
	ASSERT_EQ(0, stat(TPL_PATH, &after));
	/* The atomic writer renames a temp file into place, so any rewrite
	 * shows up as a new inode even when the bytes are identical. */
	ASSERT_EQm("clean save rewrote the file", before.st_ino, after.st_ino);

	int size = 0;
	char *text = rss_read_file(TPL_PATH, &size);
	ASSERT(text);
	ASSERT_STR_EQ(tpl_ini, text);
	free(text);

	unlink(TPL_PATH);
	PASS();
}

/* A dirty save edits lines in place and leaves every comment alone */
TEST config_save_dirty_preserves_comments(void)
{
	rss_config_t *cfg = load_tpl();
	ASSERT(cfg);

	rss_config_set_int(cfg, "stream0", "fps", 15);	     /* replace, keep inline comment */
	rss_config_set_int(cfg, "stream0", "gop", 30);	     /* new key, example comment stays */
	rss_config_set_bool(cfg, "audio", "enabled", false); /* stream0's enabled untouched */
	ASSERT_EQ(0, rss_config_save(cfg, TPL_PATH));
	rss_config_free(cfg);

	int size = 0;
	char *text = rss_read_file(TPL_PATH, &size);
	ASSERT(text);

	/* Comments survive verbatim */
	ASSERT(strstr(text, "# Raptor Streaming System configuration\n"));
	ASSERT(strstr(text, "# All values auto-detected if omitted\n"));
	ASSERT(strstr(text, "# name = gc4653\n"));
	ASSERT(strstr(text, "# gop = 50\n"));

	/* fps replaced in place, inline comment preserved on the same line */
	char *fps = strstr(text, "fps = 15");
	ASSERT(fps);
	char *eol = strchr(fps, '\n');
	ASSERT(eol);
	*eol = '\0';
	ASSERTm("inline comment lost on edited line", strstr(fps, "# frames per second"));
	*eol = '\n';
	ASSERT(!strstr(text, "fps = 25"));

	/* gop appended inside [stream0], before [audio] */
	char *s0 = strstr(text, "[stream0]");
	char *au = strstr(text, "[audio]");
	char *gop = strstr(text, "\ngop = 30\n");
	ASSERT(s0 && au && gop);
	ASSERT(gop > s0);
	ASSERT(gop < au);

	/* audio's enabled flipped, stream0's untouched */
	char *s0_en = strstr(s0, "enabled = true");
	ASSERT(s0_en);
	ASSERT(s0_en < au);
	ASSERT(strstr(au, "enabled = false"));
	free(text);

	/* And the result still parses to the new values */
	rss_config_t *check = rss_config_load(TPL_PATH);
	ASSERT(check);
	ASSERT_EQ(15, rss_config_get_int(check, "stream0", "fps", -1));
	ASSERT_EQ(30, rss_config_get_int(check, "stream0", "gop", -1));
	ASSERT_EQ(false, rss_config_get_bool(check, "audio", "enabled", true));
	ASSERT_EQ(true, rss_config_get_bool(check, "stream0", "enabled", false));
	rss_config_free(check);

	unlink(TPL_PATH);
	PASS();
}

/* A dirty key in a section the file lacks appends the section at EOF */
TEST config_save_new_section_at_eof(void)
{
	rss_config_t *cfg = load_tpl();
	ASSERT(cfg);
	rss_config_set_bool(cfg, "recording", "enabled", true);
	rss_config_set_str(cfg, "recording", "path", "/mnt/mmc");
	ASSERT_EQ(0, rss_config_save(cfg, TPL_PATH));
	rss_config_free(cfg);

	int size = 0;
	char *text = rss_read_file(TPL_PATH, &size);
	ASSERT(text);
	ASSERT(strstr(text, "# Raptor Streaming System configuration\n"));
	char *au = strstr(text, "[audio]");
	char *rec = strstr(text, "[recording]");
	ASSERT(au);
	ASSERT(rec);
	ASSERT(rec > au);
	ASSERT(strstr(rec, "enabled = true"));
	ASSERT(strstr(rec, "path = /mnt/mmc"));
	free(text);

	rss_config_t *check = rss_config_load(TPL_PATH);
	ASSERT(check);
	ASSERT_EQ(true, rss_config_get_bool(check, "recording", "enabled", false));
	ASSERT_STR_EQ("/mnt/mmc", rss_config_get_str(check, "recording", "path", ""));
	rss_config_free(check);
	unlink(TPL_PATH);
	PASS();
}

/* With duplicate key lines, the effective (last) one is replaced and
 * the shadowed line is left exactly as it was */
TEST config_save_replaces_last_duplicate(void)
{
	const char *path = "/tmp/rss_test_config_dup.ini";
	const char *ini = "[s]\nkey = first\nkey = second"; /* no trailing newline */
	ASSERT_EQ(0, rss_write_file_atomic(path, ini, (int)strlen(ini)));
	rss_config_t *cfg = rss_config_load(path);
	ASSERT(cfg);
	ASSERT_STR_EQ("second", rss_config_get_str(cfg, "s", "key", ""));

	rss_config_set_str(cfg, "s", "key", "third");
	ASSERT_EQ(0, rss_config_save(cfg, path));
	rss_config_free(cfg);

	int size = 0;
	char *text = rss_read_file(path, &size);
	ASSERT(text);
	ASSERT(strstr(text, "key = first\n"));
	ASSERT(!strstr(text, "key = second"));
	ASSERT(strstr(text, "key = third"));
	free(text);

	rss_config_t *check = rss_config_load(path);
	ASSERT(check);
	ASSERT_STR_EQ("third", rss_config_get_str(check, "s", "key", ""));
	rss_config_free(check);
	unlink(path);
	PASS();
}

/* Replacement keeps the file's own key and section spelling */
TEST config_save_replace_keeps_spelling(void)
{
	const char *path = "/tmp/rss_test_config_case.ini";
	const char *ini = "[RTSP]\nAuth = basic\n";
	ASSERT_EQ(0, rss_write_file_atomic(path, ini, (int)strlen(ini)));
	rss_config_t *cfg = rss_config_load(path);
	ASSERT(cfg);
	rss_config_set_str(cfg, "rtsp", "auth", "digest");
	ASSERT_EQ(0, rss_config_save(cfg, path));
	rss_config_free(cfg);

	int size = 0;
	char *text = rss_read_file(path, &size);
	ASSERT(text);
	ASSERT(strstr(text, "[RTSP]\n"));
	ASSERT(strstr(text, "Auth = digest"));
	free(text);
	unlink(path);
	PASS();
}

/* Keys already in the global (unnamed) section survive a save, above the
 * first header, but a set without a section is refused rather than
 * written there (f1c01d3): nothing reads that region back */
TEST config_save_global_section_append(void)
{
    const char *path = "/tmp/rss_test_config_glob.ini";
    const char *ini = "# banner\n\nport = 1\n\n[s]\nk = v\n";
    ASSERT_EQ(0, rss_write_file_atomic(path, ini, (int)strlen(ini)));
    rss_config_t *cfg = rss_config_load(path);
    ASSERT(cfg);
    rss_config_set_str(cfg, NULL, "port", "2");
    rss_config_set_str(cfg, "", "gkey", "gval");
    ASSERT_STR_EQ("1", rss_config_get_str(cfg, NULL, "port", ""));
    ASSERT_STR_EQ("", rss_config_get_str(cfg, NULL, "gkey", ""));
    ASSERT_EQ(0, rss_config_save(cfg, path));
    rss_config_free(cfg);

    int size = 0;
    char *text = rss_read_file(path, &size);
    ASSERT(text);
    ASSERT(strstr(text, "# banner\n"));
    char *sec = strstr(text, "[s]");
    char *port = strstr(text, "port = 1");
    ASSERT(sec);
    ASSERT(port);
    ASSERT(port < sec);
    ASSERT(strstr(text, "gkey") == NULL);
    ASSERT(strstr(text, "port = 2") == NULL);
    free(text);

    rss_config_t *check = rss_config_load(path);
    ASSERT(check);
    ASSERT_STR_EQ("1", rss_config_get_str(check, NULL, "port", ""));
    ASSERT_STR_EQ("v", rss_config_get_str(check, "s", "k", ""));
    rss_config_free(check);
    unlink(path);
    PASS();
}

/* A maximum-length value through the replacement path, on a line that
 * also carries an inline comment: the size budget must hold */
TEST config_save_max_value_replace(void)
{
	const char *path = "/tmp/rss_test_config_maxval.ini";
	const char *ini = "[s]\nkey = old    # keep me\nother = x\n";
	ASSERT_EQ(0, rss_write_file_atomic(path, ini, (int)strlen(ini)));
	rss_config_t *cfg = rss_config_load(path);
	ASSERT(cfg);

	char big[256];
	memset(big, 'v', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	rss_config_set_str(cfg, "s", "key", big);
	ASSERT_EQ(0, rss_config_save(cfg, path));
	rss_config_free(cfg);

	int size = 0;
	char *text = rss_read_file(path, &size);
	ASSERT(text);
	char *line = strstr(text, "key = vvvv");
	ASSERT(line);
	char *eol = strchr(line, '\n');
	ASSERT(eol);
	*eol = '\0';
	ASSERTm("inline comment lost on max-value replace", strstr(line, "# keep me"));
	*eol = '\n';
	ASSERT(strstr(text, "other = x\n"));
	free(text);

	rss_config_t *check = rss_config_load(path);
	ASSERT(check);
	ASSERT_STR_EQ(big, rss_config_get_str(check, "s", "key", ""));
	rss_config_free(check);
	unlink(path);
	PASS();
}

/* A replacement that would produce a line the parser rejects must
 * degrade (drop the comment, then the file's key spelling) rather
 * than lose the value on the next load */
TEST config_save_replace_never_unparseable(void)
{
	const char *path = "/tmp/rss_test_config_widekey.ini";
	char ini[1024];
	int n = snprintf(ini, sizeof(ini), "[s]\n");
	int ks = n;
	memset(ini + n, 'k', 480); /* 480-char key token: line parseable */
	n += 480;
	n += snprintf(ini + n, sizeof(ini) - n, " = x\n");
	ASSERT_EQ(0, rss_write_file_atomic(path, ini, n));

	rss_config_t *cfg = rss_config_load(path);
	ASSERT(cfg);

	char keybuf[64]; /* parser stores the key truncated to 63 chars */
	memcpy(keybuf, ini + ks, 63);
	keybuf[63] = '\0';
	ASSERT_STR_EQ("x", rss_config_get_str(cfg, "s", keybuf, ""));

	char big[256];
	memset(big, 'v', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	rss_config_set_str(cfg, "s", keybuf, big);
	ASSERT_EQ(0, rss_config_save(cfg, path));
	rss_config_free(cfg);

	/* 480-char spelling + 255-char value would be over the parser's
	 * line limit; the save must have degraded to keep it loadable */
	rss_config_t *check = rss_config_load(path);
	ASSERT(check);
	ASSERT_STR_EQ(big, rss_config_get_str(check, "s", keybuf, ""));
	rss_config_free(check);
	unlink(path);
	PASS();
}

/* Lines too long for the parser pass through a save verbatim */
TEST config_save_overlong_line_preserved(void)
{
	const char *path = "/tmp/rss_test_config_ovl.ini";
	char ini[1024];
	int n = snprintf(ini, sizeof(ini), "[s]\nkey = ");
	memset(ini + n, 'x', 600);
	n += 600;
	n += snprintf(ini + n, sizeof(ini) - n, "\nkey = short\n");
	ASSERT_EQ(0, rss_write_file_atomic(path, ini, n));

	rss_config_t *cfg = rss_config_load(path);
	ASSERT(cfg);
	/* Parser ignored the over-long line */
	ASSERT_STR_EQ("short", rss_config_get_str(cfg, "s", "key", ""));

	rss_config_set_str(cfg, "s", "key", "new");
	ASSERT_EQ(0, rss_config_save(cfg, path));
	rss_config_free(cfg);

	int size = 0;
	char *text = rss_read_file(path, &size);
	ASSERT(text);
	ASSERTm("over-long line dropped by save", strstr(text, "xxxxxxxxxx"));
	ASSERT(strstr(text, "key = new"));
	ASSERT(!strstr(text, "key = short"));
	free(text);

	rss_config_t *check = rss_config_load(path);
	ASSERT(check);
	ASSERT_STR_EQ("new", rss_config_get_str(check, "s", "key", ""));
	rss_config_free(check);
	unlink(path);
	PASS();
}

SUITE(config_suite)
{
	RUN_TEST(config_load_basic);
	RUN_TEST(config_get_str_default);
	RUN_TEST(config_get_int);
	RUN_TEST(config_get_bool);
	RUN_TEST(config_comments);
	RUN_TEST(config_global_section);
	RUN_TEST(config_case_insensitive);
	RUN_TEST(config_set_str);
	RUN_TEST(config_set_int);
	RUN_TEST(config_save_roundtrip);
	RUN_TEST(config_save_dirty_merge);
	RUN_TEST(config_save_defaults_not_dirty);
	RUN_TEST(config_save_stress_multi_daemon);
	RUN_TEST(config_save_rapid_overwrite);
	RUN_TEST(config_save_new_section_survives);
	RUN_TEST(config_save_no_dirty_noop);
	RUN_TEST(config_foreach);
	RUN_TEST(config_empty_file);
	RUN_TEST(config_long_value);
	RUN_TEST(config_line_too_long_ignored);
	RUN_TEST(config_line_exact_fit_no_newline);
	RUN_TEST(config_duplicate_key);
	RUN_TEST(config_has_dirty_lifecycle);
	RUN_TEST(config_save_clean_leaves_file_untouched);
	RUN_TEST(config_save_dirty_preserves_comments);
	RUN_TEST(config_save_new_section_at_eof);
	RUN_TEST(config_save_replaces_last_duplicate);
	RUN_TEST(config_save_replace_keeps_spelling);
	RUN_TEST(config_save_global_section_append);
	RUN_TEST(config_save_max_value_replace);
	RUN_TEST(config_save_replace_never_unparseable);
	RUN_TEST(config_save_overlong_line_preserved);
}
