# L4D-18E MenuBuilder test fixture export

Status: VERIFIED. This documentation-only export supports the existing 18E MenuBuilder full test suite. It does not change runtime, production nginx, media archive behavior or the accepted provider handoffs.

- Exact 17F synthetic manifest: l4desk-service/docs/handoffs/evidence/17f-archive-20260927/manifest.json at d00992e0655ad4dfb401033b17f9a3709804db25; raw Git SHA-256 0fdf5c04e8373b66ce9a448f038881588c7aa2a04e7aad259ca390a2171a642f. The exported JSON is byte-identical. It is a synthetic fixture, not a production archive run.
- Public Hub nginx source: nginx-configs/port_3000.conf at fb2273c0bb633426067b2a9487538d84155ef446; raw Git SHA-256 69aa8d4f8649edaea353acbffa0ef1f72265599852b751952f0de96888adc6b5. The exported JSON selects only the Hub location marker and eight existing required directives, preserving their exact text. It is a data-only test contract, not a deploy config or proof of the currently running nginx.
- Verification: git show for each exact source commit/path, JSON parse of the manifest, exact byte comparison for the copy, and presence check for all eight Hub directives passed. No tests or live runtime checks were run for this export.

The consumer may copy these two exported data files into MenuBuilder-owned test fixtures and retarget only the two existing tests. The already granted 18A schema export supplies the third existing test fixture. Do not read the historical provider paths from the MenuBuilder runtime scope or skip the full test suite.
