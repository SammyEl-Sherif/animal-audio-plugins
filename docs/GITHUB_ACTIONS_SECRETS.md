# GitHub Actions secrets (SammyEl-Sherif/animal-audio-plugins)

Create or update these under **Settings → Secrets and variables → Actions → Repository secrets**, or from your machine:

```bash
gh secret set SECRET_NAME -R SammyEl-Sherif/animal-audio-plugins
```

(Paste the real value when prompted; nothing secret belongs in git.)

## Supabase upload (**Release plugin** workflow, mac + win)

| Secret | Value |
| --- | --- |
| `AA_SUPABASE_ACCESS_TOKEN` | [Supabase account access token](https://supabase.com/dashboard/account/tokens) |
| `AA_SUPABASE_DB_PASSWORD` | Animal Audio project database password (`huivhdradgroykfvtclx`) |
| `AA_WEB_REPO_GITHUB_TOKEN` | GitHub PAT with **read** on `SammyEl-Sherif/animal-audio-web` (private repo checkout in CI) |

## macOS signing + notarization (**Release plugin**, platform `mac`)

| Secret | Value |
| --- | --- |
| `AA_CODESIGN_IDENTITY` | Exact Keychain name, e.g. `Developer ID Application: WORKPACE TECHNOLOGY LLC (662LDZCGT9)` |
| `AA_INSTALLER_CODESIGN_IDENTITY` | e.g. `Developer ID Installer: WORKPACE TECHNOLOGY LLC (662LDZCGT9)` |
| `AA_NOTARY_KEY_ID` | App Store Connect API key ID (10 chars) |
| `AA_NOTARY_ISSUER` | App Store Connect Issuer ID (UUID) |
| `AA_NOTARY_P8_BASE64` | `base64 -i ~/.appstoreconnect/AuthKey_XXXX.p8 \| pbcopy` |
| `AA_DEVELOPER_ID_P12_BASE64` | Base64 of exported **Developer ID Application** `.p12` |
| `AA_DEVELOPER_ID_P12_PASSWORD` | Password used when exporting the `.p12` |
| `AA_DEVELOPER_ID_INSTALLER_P12_BASE64` | Base64 of exported **Developer ID Installer** `.p12` (cert + private key; `productsign` needs it) |
| `AA_DEVELOPER_ID_INSTALLER_P12_PASSWORD` | Optional; password for the Installer `.p12` (defaults to `AA_DEVELOPER_ID_P12_PASSWORD`) |

Windows release does not need Apple signing secrets.
