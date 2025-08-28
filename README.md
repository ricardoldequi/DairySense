# DairySense
# TCC Monitoramento de Gado Leiteiro


## Estrutura do Repositório

## Submódulos

### Frontend
- Localizado na pasta `frontend/`.
- É a **interface web** do projeto.
- Permite visualizar dados coletados pelos colares, gráficos de atividade das vacas e gerenciamento do sistema.

### Backend
- Localizado na pasta `backend/`.
- É a **API e servidor** do projeto, desenvolvido em Rails.
- Responsável por receber os dados dos sensores, armazenar no banco de dados PostgreSQL e disponibilizar endpoints para o frontend.

---

## Como clonar este repositório

Para clonar corretamente o projeto com os submódulos, utilize:

```bash
git clone --recurse-submodules https://github.com/seu-usuario/tcc-monitoramento-gado.git
```
Se você já clonou sem --recurse-submodules, pode inicializar os submódulos depois:

```bash
git submodule update --init --recursive
