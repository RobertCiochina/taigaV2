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

