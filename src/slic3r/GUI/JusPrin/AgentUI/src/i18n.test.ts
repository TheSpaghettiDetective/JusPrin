import { t } from '@lingui/core/macro';
import { activateLanguage, decimalText, pageLanguage } from './i18n';

describe('page language', () => {
  afterEach(() => { activateLanguage('en'); });

  it('reads the catalog the app names, and English without one', () => {
    expect(pageLanguage('de').code).toBe('de');
    expect(pageLanguage('pt_BR').code).toBe('pt_BR');
    expect(pageLanguage('pt-BR').tag).toBe('pt-BR');
    expect(pageLanguage('de_AT').code).toBe('de');
    expect(pageLanguage('zh_HK').code).toBe('zh_TW');
    expect(pageLanguage('zh').code).toBe('zh_CN');
    expect(pageLanguage('xx').code).toBe('en');
    expect(pageLanguage(null).code).toBe('en');
  });

  it('translates marked text once a language is active', () => {
    expect(t`Language`).toBe('Language');
    activateLanguage('de');
    expect(t`Language`).toBe('Sprache');
    expect(document.documentElement.lang).toBe('de');
  });

  it('words a printer model apart from an AI model', () => {
    activateLanguage('zh_CN');
    expect(t`Model`).toBe('模型');
    expect(t`Search models`).toBe('搜索模型');
    expect(t({ message: 'Model', context: 'printer' })).toBe('型号');
    expect(t({ message: 'Search models', context: 'printer' })).toBe('搜索型号');
  });

  it('writes a decimal the way the page language does', () => {
    expect(decimalText(0.4, 1, 2)).toBe('0.4');
    expect(decimalText(1, 1, 2)).toBe('1.0');
    activateLanguage('de');
    expect(decimalText(0.4, 1, 2)).toBe('0,4');
    expect(decimalText(0.25, 1, 2)).toBe('0,25');
  });

  it('words a printer\'s build plate apart from a project plate', () => {
    activateLanguage('th');
    expect(t`Plate`).toBe('เพลต');
    expect(t({ message: 'Plate', context: 'build plate' })).toBe('แผ่นฐานพิมพ์');
  });
});
