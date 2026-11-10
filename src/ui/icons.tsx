/** Original line icons (24x24). */
const P = { fill: 'none', stroke: 'currentColor', strokeWidth: 1.8, strokeLinecap: 'round', strokeLinejoin: 'round' } as const;

export const Icons = {
  select: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path {...P} d="M5 3l13 8-6 1.5L9 19z" /></svg>
  ),
  rectangle: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path {...P} d="M3 15l9-5 9 5-9 5z" /><circle cx="3" cy="15" r="1.6" fill="currentColor" /><circle cx="21" cy="15" r="1.6" fill="currentColor" /></svg>
  ),
  pushpull: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path {...P} d="M4 16l8 4 8-4M4 16l8-4 8 4" /><path {...P} d="M12 13V3M8.5 6.5L12 3l3.5 3.5" /></svg>
  ),
  orbit: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><ellipse {...P} cx="12" cy="12" rx="9" ry="4" /><path {...P} d="M18 7.5l2.5 1.5-1 2.7" /><circle cx="12" cy="12" r="2" fill="currentColor" /></svg>
  ),
  fit: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path {...P} d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5" /><rect {...P} x="8.5" y="8.5" width="7" height="7" /></svg>
  ),
  snap: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path {...P} d="M3 8h18M3 16h18M8 3v18M16 3v18" /><circle cx="16" cy="16" r="2.4" fill="currentColor" /></svg>
  ),
  undo: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path {...P} d="M9 14L4 9l5-5" /><path {...P} d="M4 9h10a6 6 0 010 12h-3" /></svg>
  ),
  redo: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path {...P} d="M15 14l5-5-5-5" /><path {...P} d="M20 9H10a6 6 0 000 12h3" /></svg>
  ),
  newFile: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path {...P} d="M6 3h8l4 4v14H6z" /><path {...P} d="M14 3v4h4M12 11v6M9 14h6" /></svg>
  ),
  open: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path {...P} d="M3 7h6l2 2h10v10H3z" /></svg>
  ),
  save: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path {...P} d="M12 4v11M7.5 10.5L12 15l4.5-4.5M5 20h14" /></svg>
  ),
  trash: (
    <svg viewBox="0 0 24 24" aria-hidden="true"><path {...P} d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13" /></svg>
  ),
};
