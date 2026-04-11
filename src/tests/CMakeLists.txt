enable_testing()

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

add_custom_target(taiga_all_tests)
add_dependencies(
  taiga_all_tests
  anitomy-tests
  taiga.tests.ui_title
  taiga.tests.search_toolbar
  taiga.tests.list_title_color
  taiga.tests.search_my_list_only
  taiga.tests.search_load_my_list_instant
  taiga.tests.season_browse_cache
  taiga.tests.auto_download_rules
  taiga.tests.anime_history_max
  taiga.tests.search_year_combo_popup)
