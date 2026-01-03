#include "lilia/view/ui/style/palette_cache.hpp"

namespace lilia::view
{

  namespace
  {
    [[nodiscard]] bool equalColors(const PaletteColors &a, const PaletteColors &b) noexcept
    {
#define X(name, defaultValue) \
  if (a.name != b.name)       \
    return false;
      LILIA_COLOR_PALETTE(X)
#undef X
      return true;
    }
  } // namespace

  PaletteCache &PaletteCache::get()
  {
    static PaletteCache instance;
    return instance;
  }

  PaletteCache::PaletteCache()
  {
    refreshFromManager();

    m_mgrListener = ColorPaletteManager::get().addListener([this]
                                                           {
    if (refreshFromManager()) notifyListeners(); });
  }

  PaletteCache::~PaletteCache()
  {
    ColorPaletteManager::get().removeListener(m_mgrListener);
  }

  bool PaletteCache::refreshFromManager()
  {
    const PaletteColors fresh = ColorPaletteManager::get().palette();
    if (equalColors(fresh, m_colors))
      return false;
    m_colors = fresh;
    return true;
  }

  PaletteCache::ListenerID PaletteCache::addListener(std::function<void()> fn)
  {
    const ListenerID id = m_next++;
    m_listeners.emplace(id, std::move(fn));
    return id;
  }

  void PaletteCache::removeListener(ListenerID id)
  {
    m_listeners.erase(id);
  }

  void PaletteCache::notifyListeners()
  {
    std::vector<ListenerID> ids;
    ids.reserve(m_listeners.size());
    for (const auto &[id, _] : m_listeners)
      ids.push_back(id);

    for (ListenerID id : ids)
    {
      auto it = m_listeners.find(id);
      if (it != m_listeners.end() && it->second)
        it->second();
    }
  }

} // namespace lilia::view
