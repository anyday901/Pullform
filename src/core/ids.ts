/** Stable object IDs are allocated from the document's own counter so they survive save/reload. */
export const OBJECT_ID_PATTERN = /^obj-([1-9]\d*)$/;

export function formatObjectId(n: number): string {
  return `obj-${n}`;
}

export function objectIdNumber(id: string): number | null {
  const m = OBJECT_ID_PATTERN.exec(id);
  return m ? Number(m[1]) : null;
}
