/**
 * Length units. The model stores meters; units are converted only at input
 * and display boundaries.
 *
 * Unitless input is interpreted in the current display unit (millimeters by
 * default). Accepted suffixes: mm, cm, m, in (or "), ft (or ').
 */
export type LengthUnit = 'mm' | 'cm' | 'm' | 'in' | 'ft';

export const LENGTH_UNITS: readonly LengthUnit[] = ['mm', 'cm', 'm', 'in', 'ft'];

export const METERS_PER_UNIT: Record<LengthUnit, number> = {
  mm: 0.001,
  cm: 0.01,
  m: 1,
  in: 0.0254,
  ft: 0.3048,
};

export type LengthParse =
  | { ok: true; meters: number }
  | { ok: false; error: string };

const SUFFIX_ALIASES: Record<string, LengthUnit> = {
  mm: 'mm',
  cm: 'cm',
  m: 'm',
  in: 'in',
  '"': 'in',
  ft: 'ft',
  "'": 'ft',
};

const LENGTH_PATTERN = /^([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?)\s*(mm|cm|m|in|ft|"|')?$/i;

export const LENGTH_FORMAT_HINT =
  'Enter a number with an optional unit, for example 2000, 2000mm, 200cm, 2m, 78.74in or 6.5ft.';

export function parseLength(input: string, defaultUnit: LengthUnit = 'mm'): LengthParse {
  const text = input.trim();
  if (text === '') return { ok: false, error: 'Enter a length.' };
  if (/^[+-]?(inf|infinity|nan)/i.test(text)) {
    return { ok: false, error: 'Length must be a finite number.' };
  }
  if (/\d\s*'\s*\d/.test(text)) {
    return {
      ok: false,
      error: `Compound feet-and-inches like 6'2" is not supported yet. ${LENGTH_FORMAT_HINT}`,
    };
  }
  if (/\d,\d/.test(text)) {
    return { ok: false, error: `Use "." as the decimal separator. ${LENGTH_FORMAT_HINT}` };
  }
  const match = LENGTH_PATTERN.exec(text);
  if (!match) return { ok: false, error: `"${text}" is not a recognized length. ${LENGTH_FORMAT_HINT}` };
  const value = Number(match[1]);
  const suffix = match[2]?.toLowerCase();
  const unit = suffix ? SUFFIX_ALIASES[suffix] : defaultUnit;
  if (!unit) return { ok: false, error: `Unknown unit in "${text}". ${LENGTH_FORMAT_HINT}` };
  const meters = value * METERS_PER_UNIT[unit];
  if (!Number.isFinite(meters)) return { ok: false, error: 'Length must be a finite number.' };
  return { ok: true, meters };
}

export type LengthListParse =
  | { ok: true; values: number[] }
  | { ok: false; error: string };

/** Parses "2000, 1000" or "2m;1m" into meter values. */
export function parseLengthList(input: string, count: number, defaultUnit: LengthUnit = 'mm'): LengthListParse {
  const parts = input.split(/\s*[;,]\s*/).filter((p) => p !== '');
  if (parts.length !== count) {
    return {
      ok: false,
      error: count === 2
        ? 'Enter width and depth separated by a comma, for example 2000, 1000.'
        : `Enter ${count} values separated by commas.`,
    };
  }
  const values: number[] = [];
  for (const part of parts) {
    const r = parseLength(part, defaultUnit);
    if (!r.ok) return r;
    values.push(r.meters);
  }
  return { ok: true, values };
}

export function metersTo(meters: number, unit: LengthUnit): number {
  return meters / METERS_PER_UNIT[unit];
}

/** Formats a meter value in the display unit, trimming insignificant zeros. */
export function formatLength(meters: number, unit: LengthUnit = 'mm', withUnit = true): string {
  const value = metersTo(meters, unit);
  const decimals = unit === 'mm' ? 2 : unit === 'cm' ? 3 : 4;
  let text = value.toFixed(decimals);
  if (text.includes('.')) text = text.replace(/0+$/, '').replace(/\.$/, '');
  if (text === '-0') text = '0';
  return withUnit ? `${text} ${unit}` : text;
}
