/**
 * CommonModal - A parametric modal dialog for confirmation or alerts.
 *
 * Props:
 * - open: boolean (show/hide modal)
 * - title: string (modal title)
 * - description: string (modal body text)
 * - actions: array of { label, onClick, className, disabled } for buttons
 * - onClose: function (called when modal is dismissed)
 * - children: optional (for custom content)
 */
export function Modal({
  open,
  title,
  description,
  actions = [],
  onClose,
  children,
}) {
  if (!open) return null;
  return (
    <dialog className="modal-overlay" open aria-modal="true">
      {/* Backdrop button for closing modal on click */}
      <button
        type="button"
        aria-label="Close modal"
        tabIndex={0}
        className="modal-backdrop"
        onClick={onClose}
      />
      <div className="modal-confirm">
        {title && <div className="modal-title">{title}</div>}
        {description && <div className="modal-desc">{description}</div>}
        {children}
        <div className="modal-actions">
          {actions.map(
            ({ label, onClick, className = 'btn', disabled = false }, idx) => (
              <button
                key={label + String(idx)}
                className={className}
                onClick={onClick}
                disabled={disabled}
              >
                {label}
              </button>
            )
          )}
        </div>
      </div>
    </dialog>
  );
}
