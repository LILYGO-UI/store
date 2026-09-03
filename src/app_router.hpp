#ifndef LILYGO_UI_STORE_APP_ROUTER_HPP
#define LILYGO_UI_STORE_APP_ROUTER_HPP

enum class StorePage { catalog, installed, detail };

class AppRouter {
public:
  [[nodiscard]] StorePage page() const noexcept;
  [[nodiscard]] StorePage detail_origin() const noexcept;

  void show_catalog() noexcept;
  void show_installed() noexcept;
  void show_detail() noexcept;
  bool back() noexcept;

private:
  StorePage page_ = StorePage::catalog;
  StorePage detail_origin_ = StorePage::catalog;
};

#endif
