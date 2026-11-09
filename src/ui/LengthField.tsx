import { useEffect, useState, type KeyboardEvent } from 'react';

interface Props {
  label: string;
  /** Formatted live value shown while the field is not being edited. */
  value: string;
  onCommit: (text: string) => string | null;
  disabled?: boolean;
  testId?: string;
  hint?: string;
  inputRef?: (el: HTMLInputElement | null) => void;
  /** Measurement-box fields apply only on Enter and hand focus back to the viewport. */
  transient?: boolean;
}

/**
 * A length input that shows the live value until the user types, then validates
 * on Enter (or blur). Escape restores the live value.
 */
export function LengthField({ label, value, onCommit, disabled, testId, hint, inputRef, transient }: Props) {
  const [draft, setDraft] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  useEffect(() => {
    if (draft === null) setError(null);
  }, [value, draft]);

  const commit = (input?: HTMLInputElement) => {
    if (draft === null) return;
    const err = onCommit(draft);
    setError(err);
    if (!err) {
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
      e.currentTarget.blur();
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
        aria-invalid={error ? true : undefined}
        title={hint}
        value={draft ?? value}
        onFocus={(e) => e.currentTarget.select()}
        onChange={(e) => setDraft(e.target.value)}
        onKeyDown={onKeyDown}
        onBlur={() => {
          if (!transient && draft !== null && draft !== value) commit();
          else if (!transient || draft === '') setDraft(null);
        }}
      />
      {error && <span className="field-error" role="alert">{error}</span>}
    </label>
  );
}
