import { useEffect, useRef, useState, type KeyboardEvent } from 'react';

/** Returned by onCommit when nothing was applied yet and the typed text must stay (e.g. focus moved to the next field). */
export const KEEP_DRAFT = Symbol('keep-draft');

interface Props {
  label: string;
  /** Formatted live value shown while the field is not being edited. */
  value: string;
  onCommit: (text: string) => string | null | typeof KEEP_DRAFT;
  disabled?: boolean;
  testId?: string;
  hint?: string;
  inputRef?: (el: HTMLInputElement | null) => void;
  /** Measurement-box fields apply only on Enter and hand focus back to the viewport. */
  transient?: boolean;
  /** When this value changes, any typed text is discarded and the live value shows again. */
  resetKey?: unknown;
}

/**
 * A length input that shows the live value until the user types, then validates
 * on Enter (or blur). Escape restores the live value.
 */
export function LengthField({ label, value, onCommit, disabled, testId, hint, inputRef, transient, resetKey }: Props) {
  const [draft, setDraft] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  // Set while Escape blurs the field, so the blur handler does not apply the abandoned text.
  const cancelling = useRef(false);
  useEffect(() => {
    if (draft === null) setError(null);
  }, [value, draft]);
  useEffect(() => {
    setDraft(null);
    setError(null);
  }, [resetKey]);

  const commit = (input?: HTMLInputElement) => {
    if (draft === null) return;
    const result = onCommit(draft);
    if (result === KEEP_DRAFT) {
      setError(null);
      return;
    }
    setError(result);
    if (!result) {
      setDraft(null);
      if (transient && input && document.activeElement === input) input.blur();
    }
  };
  const onKeyDown = (e: KeyboardEvent<HTMLInputElement>) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      commit(e.currentTarget);
    } else if (e.key === 'Escape') {
      e.preventDefault();
      setDraft(null);
      setError(null);
      cancelling.current = true;
      e.currentTarget.blur();
      cancelling.current = false;
    }
  };
  return (
    <label className={error ? 'length-field invalid' : 'length-field'}>
      <span className="field-label">{label}</span>
      <input
        ref={inputRef}
        type="text"
        inputMode="decimal"
        spellCheck={false}
        autoComplete="off"
        disabled={disabled}
        data-testid={testId}
        data-edited={draft !== null ? 'true' : undefined}
        aria-invalid={error ? true : undefined}
        title={hint}
        value={draft ?? value}
        onFocus={(e) => e.currentTarget.select()}
        onChange={(e) => setDraft(e.target.value)}
        onKeyDown={onKeyDown}
        onBlur={() => {
          if (cancelling.current) return;
          if (!transient && draft !== null && draft !== value) commit();
          else if (!transient || draft === '') setDraft(null);
        }}
      />
      {error && <span className="field-error" role="alert">{error}</span>}
    </label>
  );
}
