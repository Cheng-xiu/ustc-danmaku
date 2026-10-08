export type ControlMode = 'touch' | 'desktop';
export type ControlModeDetection = { mode: ControlMode; reason: string };
export type ControlModeSignals = {
  userAgent: string;
  platform: string;
  maxTouchPoints: number;
  coarsePointer: boolean;
  noHover: boolean;
  userAgentMobile?: boolean;
};

/** Input capabilities decide the default; screen width alone never enables touch. */
export function detectControlModeFromSignals(signals: ControlModeSignals): ControlModeDetection {
  const points = Number.isFinite(signals.maxTouchPoints) ? Math.max(0, signals.maxTouchPoints) : 0;
  if (points === 0) return { mode: 'desktop', reason: 'no-touch-capability' };

  if (signals.userAgentMobile === true) return { mode: 'touch', reason: 'ua-client-hints-mobile-with-touch' };
  if (/iPhone|iPod|Android.*Mobile|Windows Phone/i.test(signals.userAgent)) {
    return { mode: 'touch', reason: 'phone-user-agent-with-touch' };
  }
  // iPadOS may report a desktop Safari UA and MacIntel platform. Desktop Macs
  // do not expose a multitouch screen through maxTouchPoints.
  if (/iPad/i.test(signals.userAgent) || (/^MacIntel$/i.test(signals.platform) && points > 1)) {
    return { mode: 'touch', reason: 'ipad-with-touch-including-desktop-user-agent' };
  }
  if (/Android/i.test(signals.userAgent) && signals.coarsePointer) {
    return { mode: 'touch', reason: 'android-tablet-with-coarse-touch' };
  }
  if (signals.coarsePointer && signals.noHover) {
    return { mode: 'touch', reason: 'primary-coarse-touch-without-hover' };
  }
  return { mode: 'desktop', reason: 'primary-desktop-pointer' };
}

export function detectControlMode(): ControlModeDetection {
  if (typeof navigator === 'undefined') return { mode: 'desktop', reason: 'browser-signals-unavailable' };
  const nav = navigator as Navigator & { userAgentData?: { mobile?: boolean } };
  return detectControlModeFromSignals({
    userAgent: nav.userAgent,
    platform: nav.platform,
    maxTouchPoints: nav.maxTouchPoints,
    coarsePointer: typeof matchMedia === 'function' && matchMedia('(pointer: coarse)').matches,
    noHover: typeof matchMedia === 'function' && matchMedia('(hover: none)').matches,
    userAgentMobile: nav.userAgentData?.mobile,
  });
}
