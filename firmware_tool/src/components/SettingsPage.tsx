import React from 'react';
import { Download, Eraser, Eye, EyeOff, RefreshCcw, Save, Upload } from 'lucide-react';
import useDeviceTools from '../hooks/useDeviceTools';
import { useToast } from '../context/ToastContext';
import { Dropdown } from './ui/Dropdown';
import {
  parseSettings,
  requestSettingsJson,
  sameSettingValue,
  settingsResultSchema,
  settingsSaveCommands,
  settingsValuesSchema,
  type ListItemField,
  type ListValue,
  type SettingsField,
  type SettingsMetadata,
  type SettingsValues,
} from '../services/settingsProtocol';
import { createSettingsBackup, mergeSettingsBackup } from '../services/settingsBackup';

const INPUT_CLASSES =
  'w-full rounded-lg border border-gray-600 bg-[var(--color-bg-tertiary)] px-3 py-1.5 text-sm text-[var(--color-text-primary)] focus:ring-2 focus:ring-blue-500 disabled:opacity-50';

function listCell(item: ListItemField, value: string | number | undefined) {
  if (item.secret) return '••••••';
  if (item.type === 'integer')
    return item.options.find((option) => option.value === value)?.label ?? String(value);
  return String(value ?? '');
}

const SettingsPage: React.FC<unknown> = () => {
  const { deviceInfo, flashProgress, espService } = useDeviceTools();
  const { toast } = useToast();
  const [metadata, setMetadata] = React.useState<SettingsMetadata | null>(null);
  const [values, setValues] = React.useState<SettingsValues>({});
  const [busy, setBusy] = React.useState(false);
  const [error, setError] = React.useState('');
  const [visible, setVisible] = React.useState<Record<string, boolean>>({});
  const backupInput = React.useRef<HTMLInputElement | null>(null);
  const [restoreSummary, setRestoreSummary] = React.useState('');
  const active = React.useRef<AbortController | null>(null);
  const schema = React.useMemo(
    () => (metadata ? settingsValuesSchema(metadata) : null),
    [metadata],
  );
  const available =
    deviceInfo.connected && deviceInfo.hasFirmware !== false && flashProgress.status === 'idle';
  const disabled = !available || busy;

  const load = React.useCallback(
    async (signal: AbortSignal, updateForm = true) => {
      const loaded = parseSettings(
        await requestSettingsJson(espService, 'settings', 'settings', signal),
      );
      if (signal.aborted) return;
      if (updateForm) {
        setMetadata(loaded);
        setValues(loaded.values);
      }
      return loaded;
    },
    [espService],
  );

  const run = React.useCallback(
    async (operation: 'load' | 'save' | 'reset' | 'backup' | 'restore' = 'load', file?: File) => {
      if (!available || active.current) return;
      const controller = new AbortController();
      active.current = controller;
      setBusy(true);
      setError('');
      setRestoreSummary('');
      const save = operation === 'save';
      try {
        let backup: unknown;
        if (operation === 'restore') {
          if (!file || file.size > 65536)
            throw new Error('Choose a settings backup smaller than 64 KB.');
          backup = JSON.parse(await file.text());
          if (controller.signal.aborted) return;
        }
        if (operation === 'reset') {
          await espService.sendCommand('erase', true);
          if (controller.signal.aborted) return;
          setMetadata(null);
          setValues({});
          await new Promise<void>((resolve, reject) => {
            const abort = () => {
              clearTimeout(timer);
              reject(new Error('Settings request cancelled'));
            };
            const timer = setTimeout(() => {
              controller.signal.removeEventListener('abort', abort);
              resolve();
            }, 4000);
            controller.signal.addEventListener('abort', abort, { once: true });
            if (controller.signal.aborted) abort();
          });
        }
        if (save && metadata && schema) {
          const validated = schema.parse(values);
          const patch = Object.fromEntries(
            Object.entries(validated).filter(
              ([key, value]) => !sameSettingValue(value, metadata.values[key]),
            ),
          );
          for (const command of settingsSaveCommands(patch, metadata)) {
            const result = settingsResultSchema.parse(
              await requestSettingsJson(espService, command, 'settings_result', controller.signal),
            );
            if (!result.ok) throw new Error(result.error || 'Device rejected settings');
          }
        }
        const loaded = await load(
          controller.signal,
          operation !== 'backup' && operation !== 'restore',
        );
        if (!loaded || controller.signal.aborted) return;
        if (operation === 'backup') {
          const blob = new Blob([createSettingsBackup(loaded, deviceInfo.version)], {
            type: 'application/json',
          });
          const url = URL.createObjectURL(blob);
          const link = document.createElement('a');
          link.href = url;
          link.download = `pubmote-settings-${new Date().toISOString().slice(0, 10)}.json`;
          link.click();
          setTimeout(() => URL.revokeObjectURL(url), 0);
        }
        if (operation === 'restore') {
          const merged = mergeSettingsBackup(loaded, backup);
          setMetadata(loaded);
          setValues(merged.values);
          let summary = 'No compatible settings found. Current device values were kept.';
          if (merged.restored)
            summary = `Loaded ${merged.restored} compatible settings. Review the values and press Save to apply.`;
          if (merged.skipped.length)
            summary += ` Skipped unknown or incompatible fields: ${merged.skipped.join(', ')}.`;
          if (merged.newer)
            summary += ' This backup is from newer firmware, so some settings may not restore.';
          setRestoreSummary(summary);
        }
        if (save && !controller.signal.aborted) toast.success('Settings saved', 4000);
      } catch (failure) {
        if (!controller.signal.aborted) {
          const message = failure instanceof Error ? failure.message : 'Unable to load settings';
          setError(message);
          // A failed save can leave some values applied, so force a reload.
          if (save) {
            setMetadata(null);
            try {
              await load(controller.signal);
            } catch {
              /* Keep the original error visible. */
            }
          } else if (operation !== 'restore' && operation !== 'backup') setMetadata(null);
        }
      } finally {
        if (active.current === controller) {
          active.current = null;
          setBusy(false);
        }
      }
    },
    [available, deviceInfo.version, espService, load, metadata, schema, toast, values],
  );

  React.useEffect(() => {
    setMetadata(null);
    setValues({});
    setVisible({});
    setRestoreSummary('');
    setError('');
    if (available) void run();
    return () => {
      active.current?.abort();
      active.current = null;
      setBusy(false);
    };
    // Fetch on connection/flash transitions, not on every form edit.
    // oxlint-disable-next-line react-hooks/exhaustive-deps
  }, [available, espService]);

  const renderField = (field: SettingsField) => {
    const locked = disabled || field.readOnly;
    switch (field.type) {
      case 'list': {
        const rows = (values[field.key] as ListValue | undefined) ?? [];
        if (!rows.length)
          return (
            <p id={`setting-${field.key}`} className="text-sm">
              None
            </p>
          );
        return (
          <div className="overflow-x-auto">
            <table id={`setting-${field.key}`} className="w-full text-sm">
              <thead>
                <tr>
                  {field.items.map((item) => (
                    <th
                      key={item.key}
                      className="pr-3 text-left font-normal text-[var(--color-text-secondary)]"
                    >
                      {item.label}
                    </th>
                  ))}
                </tr>
              </thead>
              <tbody>
                {rows.map((row, index) => (
                  <tr key={index}>
                    {field.items.map((item) => (
                      <td key={item.key} className="pr-3">
                        {listCell(item, row[item.key])}
                      </td>
                    ))}
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        );
      }
      case 'integer':
        return (
          <Dropdown
            id={`setting-${field.key}`}
            label={
              field.options.find((option) => option.value === values[field.key])?.label ||
              'Choose a value'
            }
            disabled={locked}
            value={String(values[field.key])}
            options={field.options.map((option) => ({
              ...option,
              value: String(option.value),
            }))}
            onChange={(value) =>
              setValues((previous) => ({
                ...previous,
                [field.key]: Number(value),
              }))
            }
          />
        );
      case 'range':
        return (
          <input
            id={`setting-${field.key}`}
            type={field.color ? 'color' : 'number'}
            min={field.min}
            max={field.max}
            step={1}
            disabled={locked}
            value={
              field.color
                ? `#${Number(values[field.key]).toString(16).padStart(6, '0')}`
                : ((values[field.key] as number | string | undefined) ?? '')
            }
            onChange={(event) => {
              let value: string | number = event.target.value;
              if (field.color) value = parseInt(value.slice(1), 16);
              else if (value !== '') value = event.target.valueAsNumber;
              setValues((previous) => ({ ...previous, [field.key]: value }));
            }}
            className={INPUT_CLASSES}
          />
        );
      case 'string':
        return (
          <div className="relative">
            <input
              id={`setting-${field.key}`}
              type={field.secret && !visible[field.key] ? 'password' : 'text'}
              autoComplete="off"
              disabled={locked}
              value={(values[field.key] as string | undefined) ?? ''}
              onChange={(event) =>
                setValues((previous) => ({
                  ...previous,
                  [field.key]: event.target.value,
                }))
              }
              className={`${INPUT_CLASSES} ${field.secret ? 'pr-10' : ''}`}
            />
            {field.secret && (
              <button
                type="button"
                aria-label={visible[field.key] ? 'Hide password' : 'Show password'}
                className="absolute inset-y-0 right-0 flex items-center pr-3"
                onClick={() =>
                  setVisible((previous) => ({
                    ...previous,
                    [field.key]: !previous[field.key],
                  }))
                }
              >
                {visible[field.key] ? <EyeOff className="h-4 w-4" /> : <Eye className="h-4 w-4" />}
              </button>
            )}
          </div>
        );
    }
  };

  const validation = schema?.safeParse(values);
  const changed = metadata?.fields.some(
    (field) => !sameSettingValue(values[field.key], metadata.values[field.key]),
  );
  const groups = [...new Set(metadata?.fields.map((field) => field.group) || [])];

  return (
    <div className="rounded-lg bg-[var(--color-bg-secondary)] p-6">
      <div className="flex items-center justify-between mb-4">
        <h2 className="text-2xl font-bold">Remote Settings</h2>
        <button
          onClick={() => void run()}
          disabled={disabled}
          title="Reload settings"
          aria-label="Reload settings"
          className="disabled:opacity-50 disabled:cursor-not-allowed"
        >
          <RefreshCcw className="h-5 w-5" />
        </button>
      </div>
      {busy && <p role="status">Loading settings...</p>}
      {error && (
        <p role="alert" className="my-3 text-red-400">
          {error}
        </p>
      )}
      <div className="mb-6 space-y-2">
        <div className="flex flex-wrap gap-3">
          <button
            disabled={disabled || !metadata}
            onClick={() => void run('backup')}
            className="flex items-center gap-2 rounded-lg px-4 py-2 text-sm bg-[var(--color-bg-tertiary)] disabled:opacity-50"
          >
            <Download className="h-4 w-4" /> Save config backup
          </button>
          <button
            disabled={disabled || !metadata}
            onClick={() => backupInput.current?.click()}
            className="flex items-center gap-2 rounded-lg px-4 py-2 text-sm bg-[var(--color-bg-tertiary)] disabled:opacity-50"
          >
            <Upload className="h-4 w-4" /> Restore config backup
          </button>
          <input
            ref={backupInput}
            type="file"
            accept=".json,application/json"
            className="hidden"
            aria-label="Choose settings backup"
            onChange={(event) => {
              const file = event.target.files?.[0];
              event.target.value = '';
              if (file) void run('restore', file);
            }}
          />
        </div>
        <p className="text-sm text-[var(--color-text-secondary)]">
          Backups contain saved device settings, including Wi-Fi credentials.
        </p>
        {restoreSummary && (
          <p role="status" className="text-sm">
            {restoreSummary}
          </p>
        )}
      </div>
      <div className="space-y-6">
        {groups.map((group) => (
          <section key={group}>
            <h3 className="font-medium mb-3">{group}</h3>
            <div className="grid gap-3 sm:grid-cols-2">
              {metadata?.fields
                .filter((field) => field.group === group)
                .map((field) => (
                  <div key={field.key} className={field.type === 'list' ? 'sm:col-span-2' : ''}>
                    <label htmlFor={`setting-${field.key}`} className="block text-sm mb-1">
                      {field.label}
                    </label>
                    {renderField(field)}
                    <p className="mt-1 text-sm text-[var(--color-text-secondary)]">
                      {field.description}
                    </p>
                  </div>
                ))}
            </div>
          </section>
        ))}
        {metadata?.warning && <p className="text-sm text-yellow-300">{metadata.warning}</p>}
        {validation && !validation.success && (
          <p role="alert" className="text-sm text-red-400">
            {validation.error.issues.map((issue) => issue.message).join('. ')}
          </p>
        )}
        <div className="border-t pt-4 flex items-center justify-between gap-4">
          <button
            disabled={disabled}
            className="flex items-center gap-2 rounded-lg px-4 py-2 text-sm bg-[var(--color-danger)]"
            onClick={() => void run('reset')}
          >
            <Eraser className="h-4 w-4" /> Factory Reset
          </button>
          <button
            onClick={() => void run('save')}
            disabled={disabled || !changed || !validation?.success}
            className="flex items-center gap-2 rounded-lg px-4 py-2 text-sm bg-blue-600 disabled:opacity-50"
          >
            <Save className="h-4 w-4" /> Save
          </button>
        </div>
      </div>
    </div>
  );
};
export default SettingsPage;
