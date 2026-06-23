# VS Code + GitHub 操作ガイド

この環境では、VS Codeの画面操作と統合ターミナルの両方からGitHubを扱えます。

## 導入済み

- Git 2.52
- Git Credential Manager
- GitHub CLI（`gh`）
- GitHub Pull Requests and Issues
- GitHub Actions
- GitHub Repositories
- GitLens

GitHub CLIは `Xenoah` アカウントで認証済みです。

## VS Codeを再読み込み

拡張機能やPATHを反映するため、コマンドパレットから次を実行します。

```text
Developer: Reload Window
```

GitHub Pull Requestsビューでサインインを求められた場合は、
`Xenoah` アカウントでブラウザ認証してください。

## 日常の基本操作

### 変更を確認

左側の「ソース管理」を開くか、ターミナルで次を実行します。

```powershell
git status -sb
git diff
```

### ブランチを作成

`main`へ直接作業せず、機能ごとのブランチを作る場合:

```powershell
git switch main
git pull --ff-only
git switch -c feature/機能名
```

VS Code左下のブランチ名からも作成・切り替えできます。

### commitとpush

```powershell
git add 変更したファイル
git commit -m "変更内容"
git push
```

新しいブランチは `push.autoSetupRemote=true` により、最初の `git push` で
自動的にupstreamが設定されます。

### Pull Requestを作成

```powershell
gh pr create --web
```

または、VS Codeの「GitHub Pull Requests」ビューから作成できます。

### Pull Requestを確認

```powershell
gh pr status
gh pr view --web
gh pr checks
```

### Issueを操作

```powershell
gh issue list
gh issue view ISSUE_NUMBER --web
gh issue create --web
```

### GitHub Actionsを確認

```powershell
gh run list
gh run view RUN_ID
gh run watch RUN_ID
```

VS Codeの「GitHub Actions」ビューからワークフローとログも確認できます。

### Releaseを作成

```powershell
gh release create TAG ファイル `
  --title "表示名" `
  --notes-file RELEASE_NOTES.md `
  --prerelease
```

## VS Codeタスク

`Terminal: Run Task` から次を実行できます。

- `Git: status`
- `Git: fetch all + prune`
- `Git: pull (fast-forward only)`
- `Git: push current branch`
- `GitHub: repository in browser`
- `GitHub: PR status`
- `GitHub: create PR in browser`
- `GitHub: current PR in browser`
- `GitHub: Actions runs`
- `GitHub: issues`

## 設定された安全策

- 新規リポジトリの既定ブランチは `main`
- `git pull` はfast-forwardのみ
- 新しいブランチの初回pushでupstreamを自動設定
- fetch時に削除済みリモートブランチ・タグを整理
- conflict表示は `zdiff3`
- rerereで過去の競合解決を再利用
- Git・GitHub CLIのエディタは `code --wait`
- VS CodeのSync操作は確認ダイアログを維持
- Smart Commitは無効。明示的にstageした内容だけcommit

## よく使う短縮コマンド

```powershell
gh co 123
gh pv
gh pc
gh rv
gh runs
```

- `gh co`: PR番号をcheckout
- `gh pv`: 現在のPRをブラウザで開く
- `gh pc`: PR作成画面を開く
- `gh rv`: リポジトリをブラウザで開く
- `gh runs`: Actions実行一覧

## 認証確認

```powershell
gh auth status
git credential-manager --version
```

GitHub CLIの認証をやり直す場合:

```powershell
gh auth login --hostname github.com --git-protocol https --web
gh auth setup-git
```
