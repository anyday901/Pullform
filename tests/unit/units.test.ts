import { describe, expect, it } from 'vitest';
import { formatLength, parseLength, parseLengthList } from '../../src/core/units';

describe('length parsing', () => {
  it('treats equivalent inputs as the same length', () => {
    const values = ['2000', '2000mm', '2000 mm', '200cm', '2m', '2 M', '+2m'].map((t) => parseLength(t));
    for (const v of values) {
      expect(v.ok).toBe(true);
      if (v.ok) expect(v.meters).toBeCloseTo(2, 12);
    }
  });

  it('supports inches and feet', () => {
    const inch = parseLength('10in');
    const inch2 = parseLength('10"');
    const ft = parseLength('2ft');
    const ft2 = parseLength("2'");
    expect(inch.ok && inch.meters).toBeCloseTo(0.254, 12);
    expect(inch2.ok && inch2.meters).toBeCloseTo(0.254, 12);
    expect(ft.ok && ft.meters).toBeCloseTo(0.6096, 12);
    expect(ft2.ok && ft2.meters).toBeCloseTo(0.6096, 12);
  });

  it('interprets unitless values in the display unit', () => {
    const r = parseLength('1.5', 'm');
    expect(r.ok && r.meters).toBe(1.5);
    const mm = parseLength('750');
    expect(mm.ok && mm.meters).toBeCloseTo(0.75, 12);
  });

  it('rejects malformed and non-finite input with a useful message', () => {
    for (const bad of ['', 'abc', '2 meters', '1e999', 'Infinity', 'NaN', '2,5', "6'2\"", '1..2', '--3', '3 km']) {
      const r = parseLength(bad);
      expect(r.ok, bad).toBe(false);
      if (!r.ok) expect(r.error.length).toBeGreaterThan(5);
    }
    const r = parseLength("6'2\"");
    expect(!r.ok && r.error).toMatch(/feet-and-inches/);
  });

  it('parses width/depth lists', () => {
    const r = parseLengthList('2000, 1m', 2);
    expect(r.ok && r.values).toEqual([2, 1]);
    expect(parseLengthList('2000', 2).ok).toBe(false);
    expect(parseLengthList('2000, x', 2).ok).toBe(false);
  });

  it('formats display values', () => {
    expect(formatLength(2)).toBe('2000 mm');
    expect(formatLength(0.75)).toBe('750 mm');
    expect(formatLength(0.12345)).toBe('123.45 mm');
    expect(formatLength(2, 'm')).toBe('2 m');
  });
});
