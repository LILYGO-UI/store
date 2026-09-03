#include "app_router.hpp"

StorePage AppRouter::page() const noexcept { return page_; }

StorePage AppRouter::detail_origin() const noexcept { return detail_origin_; }

void AppRouter::show_catalog() noexcept { page_ = StorePage::catalog; }

void AppRouter::show_installed() noexcept { page_ = StorePage::installed; }

void AppRouter::show_detail() noexcept {
  if (page_ != StorePage::detail)
    detail_origin_ = page_;
  page_ = StorePage::detail;
}

bool AppRouter::back() noexcept {
  if (page_ != StorePage::detail)
    return false;
  page_ = detail_origin_;
  return true;
}
