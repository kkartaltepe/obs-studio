#pragma once

#include "base.h"
#include "darray.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct profiler_snapshot profiler_snapshot_t;
typedef struct profiler_snapshot_entry profiler_snapshot_entry_t;
typedef struct profiler_time_entry profiler_time_entry_t;

/* ------------------------------------------------------------------------- */
/* Profiling */

EXPORT void profile_register_root(const char *name,
				  uint64_t expected_time_between_calls);

EXPORT void profile_start(const char *name);
EXPORT void profile_end(const char *name);

EXPORT void profile_reenable_thread(void);

struct profile_source_location_data {
	const char *name;
	const char *function;
	const char *file;
	uint32_t line;
	uint32_t _color; //unused
};

// Make sure preprocessor things like __LINE__, etc. resolve
#ifndef OBS_PROFILE_CONCAT_I
#define OBS_PROFILE_CONCAT_I(x, y) x##y
#endif
#ifndef OBS_PROFILE_CONCAT
#define OBS_PROFILE_CONCAT(x, y) OBS_PROFILE_CONCAT_I(x, y)
#endif

#define PROFILE_LOCATION_NAME \
	OBS_PROFILE_CONCAT(__profile_source_location, __LINE__)

#define PROFILE_LOCATION(name)                                     \
	static const struct profile_source_location_data           \
		PROFILE_LOCATION_NAME = {name, __func__, __FILE__, \
					 (uint32_t)__LINE__, 0}
#define PROFILE_STARTL_HERE(name) \
	PROFILE_LOCATION(name);   \
	profile_startL(NULL, &PROFILE_LOCATION_NAME)

#define PROFILE_START_AUTO(str)                                               \
	PROFILE_LOCATION(str);                                                \
	__attribute__((cleanup(profile_end_auto)))                            \
	const char *OBS_PROFILE_CONCAT(profile_zone_release, __LINE__) = str; \
	profile_startL(NULL, &PROFILE_LOCATION_NAME)

EXPORT void profile_startL(const char *name,
			   const struct profile_source_location_data *data);
EXPORT void profile_endL();
EXPORT void profile_annotate_text(const char *value);
EXPORT void profile_annotate_name(const char *value);

static inline void profile_end_auto(const char **unused)
{
	UNUSED_PARAMETER(unused);
	profile_endL();
}

/* ------------------------------------------------------------------------- */
/* Frame profiling */

EXPORT void profiler_frame_mark_auto(const char *name);
EXPORT void profiler_frame_mark_start(const char *name);
EXPORT void profiler_frame_mark_end(const char *name);

EXPORT void profiler_gpu_zone_start(const char *name, uint16_t tid);
EXPORT void profiler_gpu_zone_end(uint16_t tid);
EXPORT void profiler_gpu_time_report(uint16_t tid, uint64_t time);
EXPORT void profiler_gpu_ctx_new(int64_t gpu_time);

/* ------------------------------------------------------------------------- */
/* Profiler control */

EXPORT void profiler_start(void);
EXPORT void profiler_stop(void);

EXPORT void profiler_print(profiler_snapshot_t *snap);
EXPORT void profiler_print_time_between_calls(profiler_snapshot_t *snap);

EXPORT void profiler_free(void);

/* ------------------------------------------------------------------------- */
/* Profiler name storage */

typedef struct profiler_name_store profiler_name_store_t;

EXPORT profiler_name_store_t *profiler_name_store_create(void);
EXPORT void profiler_name_store_free(profiler_name_store_t *store);

#ifndef _MSC_VER
#define PRINTFATTR(f, a) __attribute__((__format__(__printf__, f, a)))
#else
#define PRINTFATTR(f, a)
#endif

PRINTFATTR(2, 3)
EXPORT const char *profile_store_name(profiler_name_store_t *store,
				      const char *format, ...);

#undef PRINTFATTR

/* ------------------------------------------------------------------------- */
/* Profiler data access */

struct profiler_time_entry {
	uint64_t time_delta;
	uint64_t count;
};

typedef DARRAY(profiler_time_entry_t) profiler_time_entries_t;

typedef bool (*profiler_entry_enum_func)(void *context,
					 profiler_snapshot_entry_t *entry);

EXPORT profiler_snapshot_t *profile_snapshot_create(void);
EXPORT void profile_snapshot_free(profiler_snapshot_t *snap);

EXPORT bool profiler_snapshot_dump_csv(const profiler_snapshot_t *snap,
				       const char *filename);
EXPORT bool profiler_snapshot_dump_csv_gz(const profiler_snapshot_t *snap,
					  const char *filename);

EXPORT size_t profiler_snapshot_num_roots(profiler_snapshot_t *snap);
EXPORT void profiler_snapshot_enumerate_roots(profiler_snapshot_t *snap,
					      profiler_entry_enum_func func,
					      void *context);

typedef bool (*profiler_name_filter_func)(void *data, const char *name,
					  bool *remove);
EXPORT void profiler_snapshot_filter_roots(profiler_snapshot_t *snap,
					   profiler_name_filter_func func,
					   void *data);

EXPORT size_t profiler_snapshot_num_children(profiler_snapshot_entry_t *entry);
EXPORT void
profiler_snapshot_enumerate_children(profiler_snapshot_entry_t *entry,
				     profiler_entry_enum_func func,
				     void *context);

EXPORT const char *
profiler_snapshot_entry_name(profiler_snapshot_entry_t *entry);

EXPORT profiler_time_entries_t *
profiler_snapshot_entry_times(profiler_snapshot_entry_t *entry);
EXPORT uint64_t
profiler_snapshot_entry_min_time(profiler_snapshot_entry_t *entry);
EXPORT uint64_t
profiler_snapshot_entry_max_time(profiler_snapshot_entry_t *entry);
EXPORT uint64_t
profiler_snapshot_entry_overall_count(profiler_snapshot_entry_t *entry);

EXPORT profiler_time_entries_t *
profiler_snapshot_entry_times_between_calls(profiler_snapshot_entry_t *entry);
EXPORT uint64_t profiler_snapshot_entry_expected_time_between_calls(
	profiler_snapshot_entry_t *entry);
EXPORT uint64_t profiler_snapshot_entry_min_time_between_calls(
	profiler_snapshot_entry_t *entry);
EXPORT uint64_t profiler_snapshot_entry_max_time_between_calls(
	profiler_snapshot_entry_t *entry);
EXPORT uint64_t profiler_snapshot_entry_overall_between_calls_count(
	profiler_snapshot_entry_t *entry);

#ifdef __cplusplus
}
#endif
