// One state input per variant of the two approval matrices on the Figma page
// "Agent UI · States & Components": the tool card (1483:344) and the plan card
// (1487:498). Each renders the production card from a ToolActivityInfo the
// host could send.

import { ToolActivityInfo, ToolStateName } from '../bridge/protocol';
import { PlanActivityCard } from '../components/PlanActivityCard';
import { ToolActivityCard } from '../components/ToolActivityCard';
import { mounted, mountedApp, VisualCase } from '../test/visual';
import { chatState, turn } from './states';

const noop = () => {};

export function toolActivity(overrides: Partial<ToolActivityInfo> = {}): ToolActivityInfo {
  return {
    actionId: 't-1', correlationId: 'm-2', server: 'jusprin-native', tool: 'duplicate_object',
    title: 'Duplicate “cube-a”', arguments: {}, actionClass: 'mutation', requiresApproval: true,
    sessionId: '1', expectedRevision: 2, state: 'pending', progress: { current: 0, total: 0 }, ...overrides,
  };
}

const tool = (id: string, name: string, expects: string[], activity: Partial<ToolActivityInfo>, readOnly = false): VisualCase => ({
  node: '1483:344', matrix: 'Tool approval', id: `tool-${id}`, name, frame: 'card', expects,
  build: () => mounted(<ToolActivityCard activity={toolActivity(activity)} onDecision={noop} onCancel={noop} readOnly={readOnly} />),
});

const steps = ['Rotate “cube-a” to 45°', 'Set brim to 5 mm', 'Set first-layer speed to 18 mm/s'];

export function planMembersIn(states: ToolStateName[], overrides: Partial<ToolActivityInfo>[] = []): ToolActivityInfo[] {
  return states.map((state, index) => toolActivity({
    actionId: `p-${index + 1}`, planId: 'plan-1', tool: 'settings_apply_patch', title: steps[index], state,
    ...overrides[index],
  }));
}

const plan = (id: string, name: string, expects: string[], members: ToolActivityInfo[],
  props: { stillProposing?: boolean; readOnly?: boolean } = {}): VisualCase => ({
  node: '1487:498', matrix: 'Plan approval', id: `plan-${id}`, name, frame: 'card', expects,
  build: () => mounted(<PlanActivityCard members={members} headline="Improve printability for “cube-a”"
    onDecision={noop} onCancel={noop} {...props} />),
});

// The four context screens (Figma 1424:396, 1452:248, 1471:282, 1471:5968):
// the same cards in the real pane, under the turn that proposed them.
const context = (node: string, id: string, name: string, expects: string[], activities: ToolActivityInfo[], proposing = false): VisualCase => ({
  node, matrix: 'Approvals in context', id: `context-${id}`, name, frame: 'pane', expects,
  build: () => mountedApp(chatState({
    conversation: [
      turn('m-1', 'user', 'This will hold a backpack. Make it strong, but keep the screw holes accurate.'),
      turn('m-2', 'assistant', activities.length === 1
        ? 'I can duplicate cube-a to create a second copy on the same plate.'
        : 'I can apply these three changes to improve printability.', proposing ? { state: 'streaming' } : {}),
    ],
    streamingMessageId: proposing ? 'm-2' : null,
    toolActivities: [
      toolActivity({ actionId: 'plan-headline', tool: 'plan_set', state: 'succeeded', requiresApproval: false,
        correlationId: 'm-0', arguments: { headline: 'Improve printability for “cube-a”' } }),
      ...activities,
    ],
  })),
});

export const approvalCases: VisualCase[] = [
  context('1424:396', 'tool-pending', 'Tool approval · pending', ['Duplicate “cube-a”', 'Approve'], [toolActivity()]),
  context('1452:248', 'plan-pending', 'Plan approval · pending', ['Improve printability for “cube-a”', 'Approve all'],
    planMembersIn(['pending', 'pending', 'pending'])),
  context('1471:282', 'plan-proposing', 'Plan approval · still proposing', ['The Agent is still adding to this plan'],
    planMembersIn(['pending', 'pending', 'pending']), true),
  context('1471:5968', 'plan-running', 'Plan approval · running', ['1 of 3', 'Cancel'], planMembersIn(['succeeded', 'running', 'approved'])),

  tool('pending', 'State=Pending · Access=Interactive', ['Waiting for your approval', 'Approve', 'Reject'], {}),
  tool('pending-read-only', 'State=Pending · Access=Read-only', ['Waiting for your approval', 'disabled'], {}, true),
  tool('approved', 'State=Approved · Access=Interactive', ['Approved', 'jp-progress-indeterminate', 'Cancel'], { state: 'approved' }),
  tool('approved-read-only', 'State=Approved · Access=Read-only', ['Approved', 'disabled'], { state: 'approved' }, true),
  tool('running', 'State=Running · Access=Interactive', ['Running… · 33%', 'aria-valuenow="1"', 'Cancel'],
    { state: 'running', progress: { current: 1, total: 3 } }),
  tool('running-read-only', 'State=Running · Access=Read-only', ['Running… · 33%', 'disabled'],
    { state: 'running', progress: { current: 1, total: 3 } }, true),
  tool('succeeded', 'State=Succeeded', ['Done', 'tool-status-icon done'], { state: 'succeeded' }),
  tool('failed', 'State=Failed · Failure=Generic', ['The action failed.', 'tool-status-icon failed'], { state: 'failed' }),
  tool('failed-stale', 'State=Failed · Failure=Stale revision', ['The project changed after this was proposed'],
    { state: 'failed', error: { code: 'stale_revision', message: 'stale' } }),
  tool('rejected', 'State=Rejected', ['Rejected — nothing was changed'], { state: 'rejected' }),
  tool('cancelled', 'State=Cancelled', ['Cancelled — nothing was changed'], { state: 'cancelled' }),

  plan('pending', 'State=Pending · Mode=Standard · Risk=Mutation', ['3 changes · changes the project', 'Waiting for your approval', 'Approve all'],
    planMembersIn(['pending', 'pending', 'pending'])),
  plan('still-proposing', 'State=Pending · Mode=Still proposing', ['The Agent is still adding to this plan', 'disabled'],
    planMembersIn(['pending', 'pending', 'pending']), { stillProposing: true }),
  plan('pending-read-only', 'State=Pending · Mode=Read-only', ['Waiting for your approval', 'disabled'],
    planMembersIn(['pending', 'pending', 'pending']), { readOnly: true }),
  plan('destructive', 'State=Pending · Risk=Destructive', ['3 changes · destructive — “Delete “cube-a”” cannot be undone in JusPrin', 'plan-member state-pending destructive'],
    planMembersIn(['pending', 'pending', 'pending'], [{ title: 'Delete “cube-a”', actionClass: 'destructive' }])),
  plan('running', 'State=Running · Mode=Standard', ['1 of 3', 'Queued', 'Running…', 'Cancel'],
    planMembersIn(['succeeded', 'running', 'approved'])),
  plan('running-read-only', 'State=Running · Mode=Read-only', ['1 of 3', 'disabled'],
    planMembersIn(['succeeded', 'running', 'approved']), { readOnly: true }),
  plan('succeeded', 'State=Succeeded', ['tool-status-icon done'], planMembersIn(['succeeded', 'succeeded', 'succeeded'])),
  plan('failed', 'State=Failed', ['Could not update the brim setting.', 'Cancelled'],
    planMembersIn(['succeeded', 'failed', 'cancelled'], [{}, { error: { code: 'failed', message: 'Could not update the brim setting.' } }])),
  plan('rejected', 'State=Rejected', ['Rejected — nothing was changed'], planMembersIn(['rejected', 'rejected', 'rejected'])),
  plan('cancelled', 'State=Cancelled', ['Cancelled'], planMembersIn(['cancelled', 'cancelled', 'cancelled'])),
];
