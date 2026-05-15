enable_testing()

# Qt tests that only need taiga-core (no taiga-gui / resources link).
function(taiga_add_qttest_core target)
  add_executable(${target})
  target_link_libraries(${target} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Sql
    Qt6::Widgets
    Qt6::Test
    taiga-core
    taiga-deps
  )
  add_test(NAME ${target} COMMAND ${target})
endfunction()

function(taiga_add_qttest target)
  add_executable(${target})
  target_link_libraries(${target} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    Qt6::Widgets
    taiga-core
    taiga-gui
    taiga-resources
  )
  add_test(NAME ${target} COMMAND ${target})
endfunction()

taiga_add_qttest(taiga.tests.ui_title)
target_sources(taiga.tests.ui_title PRIVATE gui/test_ui_title.cpp)

taiga_add_qttest(taiga.tests.search_toolbar)
target_sources(taiga.tests.search_toolbar PRIVATE gui/test_search_widget_toolbar.cpp)

taiga_add_qttest(taiga.tests.list_title_color)
target_sources(taiga.tests.list_title_color PRIVATE gui/test_list_title_color.cpp)

taiga_add_qttest(taiga.tests.card_delegate_layout)
target_sources(taiga.tests.card_delegate_layout PRIVATE gui/test_card_delegate_layout.cpp)

taiga_add_qttest(taiga.tests.navigation_sidebar_refresh)
target_sources(taiga.tests.navigation_sidebar_refresh PRIVATE gui/test_navigation_sidebar_refresh.cpp)

taiga_add_qttest(taiga.tests.search_my_list_only)
target_sources(taiga.tests.search_my_list_only PRIVATE gui/test_search_my_list_only.cpp)

taiga_add_qttest(taiga.tests.search_load_my_list_instant)
target_sources(taiga.tests.search_load_my_list_instant PRIVATE gui/test_search_load_my_list_is_instant.cpp)

taiga_add_qttest(taiga.tests.season_browse_cache)
target_sources(taiga.tests.season_browse_cache PRIVATE taiga/test_season_browse_cache.cpp)

taiga_add_qttest(taiga.tests.auto_download_rules)
target_sources(taiga.tests.auto_download_rules PRIVATE taiga/test_auto_download_rules.cpp)

taiga_add_qttest(taiga.tests.anime_history_max)
target_sources(taiga.tests.anime_history_max PRIVATE media/test_anime_history_max.cpp)

taiga_add_qttest(taiga.tests.search_year_combo_popup)
target_sources(taiga.tests.search_year_combo_popup PRIVATE gui/test_search_year_combo_popup.cpp)

taiga_add_qttest_core(taiga.tests.streaming_sites)
target_sources(taiga.tests.streaming_sites PRIVATE track/test_streaming_sites.cpp)

taiga_add_qttest_core(taiga.tests.recognition_normalize)
target_sources(taiga.tests.recognition_normalize PRIVATE track/test_recognition_normalize.cpp)

taiga_add_qttest_core(taiga.tests.episode)
target_sources(taiga.tests.episode PRIVATE track/test_episode.cpp)

taiga_add_qttest_core(taiga.tests.anime_season)
target_sources(taiga.tests.anime_season PRIVATE media/test_anime_season.cpp)

taiga_add_qttest_core(taiga.tests.anime_utils)
target_sources(taiga.tests.anime_utils PRIVATE media/test_anime_utils.cpp)

taiga_add_qttest_core(taiga.tests.string_helpers)
target_sources(taiga.tests.string_helpers PRIVATE base/test_string_helpers.cpp)

taiga_add_qttest_core(taiga.tests.anime_list_utils)
target_sources(taiga.tests.anime_list_utils PRIVATE media/test_anime_list_utils.cpp)

taiga_add_qttest_core(taiga.tests.announced_related_refresh)
target_sources(taiga.tests.announced_related_refresh PRIVATE media/test_announced_related_refresh.cpp)

taiga_add_qttest_core(taiga.tests.version)
target_sources(taiga.tests.version PRIVATE taiga/test_version.cpp)

taiga_add_qttest_core(taiga.tests.ui_strings)
target_sources(taiga.tests.ui_strings PRIVATE gui/test_ui_strings.cpp)

add_custom_target(taiga_all_tests)
add_dependencies(
  taiga_all_tests
  anitomy-tests
  taiga.tests.ui_title
  taiga.tests.search_toolbar
  taiga.tests.list_title_color
  taiga.tests.card_delegate_layout
  taiga.tests.navigation_sidebar_refresh
  taiga.tests.search_my_list_only
  taiga.tests.search_load_my_list_instant
  taiga.tests.season_browse_cache
  taiga.tests.auto_download_rules
  taiga.tests.anime_history_max
  taiga.tests.search_year_combo_popup
  taiga.tests.streaming_sites
  taiga.tests.recognition_normalize
  taiga.tests.episode
  taiga.tests.anime_season
  taiga.tests.anime_utils
  taiga.tests.string_helpers
  taiga.tests.anime_list_utils
  taiga.tests.announced_related_refresh
  taiga.tests.version
  taiga.tests.ui_strings)
