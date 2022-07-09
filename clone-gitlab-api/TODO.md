- Start process exit watcher thread
- Get /api/v4/projects/ (first page)
- This returns the total number of pages
- For each remaining page *in parallel?*:
  * Get it
- On page returned:
  * Parse body as JSON
  * Launch worker for project
