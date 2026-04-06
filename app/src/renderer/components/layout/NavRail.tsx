import type { AppPage } from '../../shared/types';
import { useNavigationContext } from '../../state/context';

const NAV_ITEMS: Array<{ page: AppPage; title: string; subtitle: string }> = [
  { page: 'router', title: 'Router', subtitle: 'Process, bind, health' },
  { page: 'accounts', title: 'Accounts', subtitle: 'Quota, tokens, usage' },
  { page: 'sessions', title: 'Sessions', subtitle: 'Affinity and sticky reuse' },
  { page: 'logs', title: 'Logs', subtitle: 'Requests and events' },
  { page: 'settings', title: 'Settings', subtitle: 'Routing, auth, keys' },
];

export function NavRail() {
  const navigation = useNavigationContext();

  return (
    <aside className="nav-rail">
      <nav className="rail-group" aria-label="Runtime objects">
        <p className="rail-label">Objects</p>
        {NAV_ITEMS.map((item) => (
          <button
            key={item.page}
            className={`nav-item${item.page === navigation.currentPage ? ' active' : ''}`}
            data-page={item.page}
            type="button"
            onClick={() => navigation.setCurrentPage(item.page)}
          >
            <strong>{item.title}</strong>
            <span>{item.subtitle}</span>
          </button>
        ))}
      </nav>
    </aside>
  );
}
