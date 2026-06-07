# Claude Code Project Directives

You are operating in an agent-agnostic repository. The core rules, boundaries, and workflows for this project are stored in the `.agents/` directory to be shared across Antigravity IDE and Claude Code.

## Mandatory Pre-flight Checklist
Before planning or executing changes in this repository, you MUST:
1. Read `Standards/repo-standards.md` to understand the required directory architecture.
2. Read all files in `.agents/rules/` to understand architectural boundaries and project constraints.
3. Check `.agents/workflows/` for established playbooks before executing complex or repetitive tasks.
4. Check `.agents/skills/` for any reusable capabilities or formatting instructions you may need to apply.

## Agent Emulation
If the user asks you to perform a specific role (e.g., DevOps Engineer), you must read the corresponding `.agent` file in the `.agents/` directory (e.g., `devops-engineer.agent`) and adopt the System Prompt, SOPs, and constraints defined within it.

## Execution Directives
- **Workflow Annotations**: If you encounter `// turbo` or `// turbo-all` in a workflow, recognize that these are Antigravity-specific directives for auto-running commands. You should treat them as comments but continue following the workflow's functional steps.
- **Strict Adherence**: Never write temporary files or logs to the repository root. Always use the `scratch/` directory (which is gitignored). Never place secrets in tracked directories; only use `configs/secrets/`.
