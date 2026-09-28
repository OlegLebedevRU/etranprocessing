# Controller review: MenuBuilder post-18E maintenance

Decision: **ACCEPT**. Reviewed branch HEAD `1edd489` (corrective code
`d823d56`). This accepts the maintenance packet after the already accepted 18E;
it does not change the 18E handoff or authorize broader commercial activation.

## Independent checks

- Read-only production SSH inspection found `menubuilder-backend` running image
  `sha256:8832945fdbc5f69c2463c7f613ced3c1d8efcee91851dfa4e44d861c2e9c15f8`
  and `processing-backend` running image
  `sha256:70e9a060b0ae65403408b6a14187afe87fa3e0d065bc612ac928998508515e33`.
- ProcessingBackend reported Alembic `028 (head)`. The installed MenuBuilder
  `auth.py` matches the corrective behavior: a missing tenant organization
  returns 404; an unavailable or invalid organization-policy row returns 503
  instead of permissive site and license defaults. The corresponding negative
  tests are in `test_tenant_navigation_policy.py`. The producer reports 565
  passing MenuBuilder tests and passing Ruff/Pyright checks.

## Runtime behavior evidence

The operator's reversible E2E on existing tenant 10000 started from
`both/null/true/true`, temporarily set
`l4desk/l4desk/false/false`, and observed matching `/api/auth/me` values under
test05. The browser hid the site switch and license entries; direct `/licenses`
and `/billing` navigation redirected to `/terminals`. The original four values
were restored and read back. Tenant 339 was not modified. This browser result
is operator-provided evidence, separate from the controller's read-only SSH
checks.

## Residual conditions

- Saving the terminal edit form has not been exercised on production. Local
  tests cover role 3/5 updates and clearing editable fields; the production
  modal was opened and closed without a write.
- Any subsequent Alembic head must be coordinated with MenuBuilder's startup
  schema guard. An old image requiring revision 027 is not a standalone rollback
  option after migration 028.
