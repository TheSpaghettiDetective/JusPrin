// The welcome-back window's page against a scripted host: what came from
// JusPrin v1, the account v1 was signed in to (checked once as the window
// opens), and the three states the canvas draws for it.

import { beforeEach, describe, expect, it } from 'vitest';
import { act, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { WelcomeApp } from './WelcomeApp';
import { WelcomeBack, WelcomeBackProps } from './components/WelcomeBack';
import { Envelope, PROTOCOL_NAME, PROTOCOL_VERSION, StatePayload, WelcomeBackPayload } from './bridge/protocol';

class MockHost {
  received: Envelope[] = [];
  transport = { post: (json: string) => { this.received.push(JSON.parse(json)); } };

  deliver(type: string, payload: unknown): void {
    const envelope: Envelope = { protocol: PROTOCOL_NAME, version: PROTOCOL_VERSION, id: `h-${this.received.length}`, type, payload };
    act(() => { window.__jusprinBridge!.deliver(envelope); });
  }

  ofType(type: string): Envelope[] {
    return this.received.filter((envelope) => envelope.type === type);
  }
}

function report(overrides: Partial<WelcomeBackPayload> = {}): WelcomeBackPayload {
  return {
    kind: 'welcome_back',
    printers: ['Bambu Lab A1', 'Prusa MK4S'],
    presets: ['Generic PLA', 'Silk PETG', '0.20 mm Strong', 'Fast draft'],
    connections: ['Prusa MK4S'],
    preferences: true,
    failed: [],
    unusable: [],
    ...overrides,
  };
}

function connect(host: MockHost, session: WelcomeBackPayload, keys: unknown[]): void {
  host.deliver('hello_ack', { version: 1, capabilities: ['cloud_account'], agent: { status: 'unavailable' }, appearance: 'light' });
  const state: Partial<StatePayload> = {
    agent: { status: 'unavailable' }, appearance: 'light', conversation: [], conversations: [], activeConversationId: '',
    session,
  };
  host.deliver('state', state);
  host.deliver('setup_keys', { keys });
}

const signedIn = { provider: 'jusprin', hint: '…wxyz', rejected: false, account: 'alex@example.com' };

describe('Welcome back', () => {
  let host: MockHost;

  beforeEach(() => {
    host = new MockHost();
  });

  it('V1-01: lists what came from v1 and checks v1’s sign-in once', async () => {
    render(<WelcomeApp getTransport={() => host.transport} />);
    connect(host, report(), [{ ...signedIn, account: undefined }]);
    expect(host.ofType('account_check')).toHaveLength(1);
    host.deliver('account_status', { phase: 'checking' });
    expect(screen.getByTestId('welcome-account')).toHaveTextContent('Checking your JusPrin v1 sign-in');

    host.deliver('account_status', { phase: 'idle' });
    host.deliver('setup_keys', { keys: [signedIn] });
    expect(host.ofType('account_check')).toHaveLength(1);
    expect(screen.getByRole('heading', { name: 'Your JusPrin v1 setup is here' })).toBeInTheDocument();
    expect(screen.getByTestId('welcome-account')).toHaveTextContent('Signed in as alex@example.com');
    expect(screen.getByText('Bambu Lab A1 · Prusa MK4S')).toBeInTheDocument();
    expect(screen.getByText('Generic PLA, Silk PETG, 0.20 mm Strong and 1 more')).toBeInTheDocument();
    expect(screen.getByText('Printer connections')).toBeInTheDocument();
    expect(screen.getByText('Units, language and recent files')).toBeInTheDocument();
    expect(screen.getByText(/Chats from JusPrin v1 stay in JusPrin v1/)).toBeInTheDocument();

    await userEvent.click(screen.getByRole('button', { name: 'Continue' }));
    expect(host.ofType('shell_action').at(-1)?.payload).toEqual({ action: 'close' });
  });

  it('V1-02: says what could not be used and counts what could', () => {
    render(<WelcomeApp getTransport={() => host.transport} />);
    connect(host, report({ unusable: ['Fast draft'] }), [signedIn]);
    expect(screen.getByRole('heading', { name: 'Most of your JusPrin v1 setup is here' })).toBeInTheDocument();
    expect(screen.getByText('3 of 4')).toBeInTheDocument();
    expect(screen.getByText(/“Fast draft” can’t be used here: it is based on a printer JusPrin doesn’t have/)).toBeInTheDocument();
  });

  it('V1-03: an ended sign-in offers signing in again in the browser', async () => {
    render(<WelcomeApp getTransport={() => host.transport} />);
    connect(host, report(), [{ ...signedIn, rejected: true }]);
    expect(screen.getByTestId('welcome-account')).toHaveTextContent('Sign in again to use AI');
    expect(screen.getByTestId('welcome-account')).toHaveTextContent('Your JusPrin v1 sign-in for alex@example.com has expired.');
    await userEvent.click(screen.getByRole('button', { name: 'Continue with JusPrin' }));
    expect(host.ofType('account_sign_in')).toHaveLength(1);
    host.deliver('account_status', { phase: 'browser', url: 'https://jusprinv2.obico.io/o/authorize/' });
    expect(screen.getByText('Finish in your browser')).toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'Cancel' }));
    expect(host.ofType('account_cancel')).toHaveLength(1);
  });

  it('1c: offers the first month when v1 was not signed in, and checks nothing', () => {
    render(<WelcomeApp getTransport={() => host.transport} />);
    connect(host, report(), []);
    expect(host.ofType('account_check')).toHaveLength(0);
    expect(screen.getByTestId('welcome-account')).toHaveTextContent('Sign up and get 500 free credits');
    expect(screen.getByTestId('welcome-account')).not.toHaveTextContent('original price');
    expect(screen.getByRole('button', { name: 'Sign in for 500 credits' })).toHaveClass('primary');
    expect(screen.getByRole('button', { name: 'Continue' })).toHaveClass('secondary');
  });

  const account = (v1_subscription: unknown) => ({ account: { credits: { total: 500, lots: [] }, plan: null, v1_subscription } });

  // Until billing moves v1 subscribers to Hobby (step 3 of the pricing plan),
  // the gateway reporting a subscription must not make the app promise a price.
  it('1d: promises no kept price yet, even when the account reports a v1 subscription', () => {
    render(<WelcomeApp getTransport={() => host.transport} />);
    connect(host, report(), [signedIn]);
    host.deliver('account', account({ plan_id: 'jusprin-unlimited-monthly', cents: 699, period: 'month' }));
    expect(screen.queryByTestId('welcome-kept-price')).toBeNull();
    expect(screen.getByTestId('welcome-back')).not.toHaveTextContent('Hobby');
    expect(screen.getByTestId('welcome-back')).not.toHaveTextContent('original');
  });

  describe('once the price carries over', () => {
    const actions = { checkAccount: () => {}, signIn: () => {}, cancelSignIn: () => {}, close: () => {} };
    const carriedOver = (subscription: WelcomeBackProps['subscription'], account: WelcomeBackProps['account']) =>
      render(<WelcomeBack report={report()} account={account} status={{ phase: 'idle' }} actions={actions}
        subscription={subscription} priceCarriesOver />);
    const signedInKey = { provider: 'jusprin', account: 'kenneth@example.com' } as WelcomeBackProps['account'];

    it('1d: a v1 subscriber is told they keep their price, with nothing to choose', () => {
      carriedOver({ plan_id: 'jusprin-unlimited-monthly', cents: 699, period: 'month' }, signedInKey);
      const box = screen.getByTestId('welcome-kept-price');
      expect(box).toHaveTextContent('Hobby, at your original $6.99 a month');
      expect(box).toHaveTextContent('750 credits a month.');
      expect(box).toHaveTextContent('Plus 500 more for your first month.');
      expect(box).toHaveTextContent('New subscribers pay $10. You keep this price while subscribed.');
      expect(box.querySelector('button')).toBeNull();
    });

    it('1d: the annual plan names its own price', () => {
      carriedOver({ plan_id: 'jusprin-unlimited-annual', cents: 5999, period: 'year' }, signedInKey);
      expect(screen.getByTestId('welcome-kept-price')).toHaveTextContent('Hobby, at your original $59.99 a year');
    });

    it('1d: anyone else sees no price', () => {
      carriedOver(null, signedInKey);
      expect(screen.queryByTestId('welcome-kept-price')).toBeNull();
    });

    it('1c: tells someone not signed in what signing in means for a v1 subscriber', () => {
      carriedOver(undefined, undefined);
      expect(screen.getByTestId('welcome-account')).toHaveTextContent(
        'Subscribed to JusPrin v1? You get these 500 credits and keep your original price, on the $10 Hobby plan.');
      // A v1 subscriber keeps their plan, so the free plan's monthly credits are not offered.
      expect(screen.getByTestId('welcome-account')).not.toHaveTextContent(/50 free credits a month/);
    });
  });
});
