// A calibration project's test, read from the workspace context the host
// sends (board 7). The names here are the host's names for its tests, not
// setting keys.

import { WorkspaceContext } from './bridge/protocol';

// A reopened calibration test: OrcaSlicer kept the tower's steps only in
// memory, so the plate no longer holds them. The pressure-advance pattern
// keeps its own in the plate's G-code, and flow tiles need none.
export function testStepsLost(context: WorkspaceContext): string {
  const test = context.calibration;
  if (!test?.test || context.plates.some((plate) => plate.calibration)) return '';
  if (test.test === 'temperature') return 'Temperature steps not kept in the file';
  if (test.test === 'pressure_advance' && test.method !== 'pattern') return 'Pressure advance steps not kept in the file';
  return '';
}
