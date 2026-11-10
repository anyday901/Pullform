import { useSyncExternalStore } from 'react';
import type { Editor, EditorState } from '../editor/Editor';

export function useEditorState(editor: Editor): EditorState {
  return useSyncExternalStore(editor.subscribe, editor.getState);
}
