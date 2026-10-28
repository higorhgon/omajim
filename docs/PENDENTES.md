# O que falta no Omajim (Qt)

O que ainda falta para a versão C++/Qt substituir o Omajim em Python
(Gajim 2.6). Tudo o que estava na lista de porte já foi feito: envio pelo
servidor, correções, links e prévias, importação do histórico do Gajim, SI com
proxy e ufw, apelidos locais, busca aproximada e Ctrl+T, seleção de mensagens,
identificação como Omajim, formatação, reações e respostas, ausência
automática, grupos de contatos, Ctrl+F, colar arquivos, seletor de emoji e
seleção de texto e de mensagens como no ZapFast.

## Planejado

Nesta ordem:

1. **Várias contas.** Hoje o app usa uma conta por vez. Falta conectar várias
   ao mesmo tempo, com histórico, roster e senha separados por conta, e mostrar
   no app a conta de cada conversa.
2. **Chamadas de voz e vídeo** (Jingle, XEP-0166/0167). O QXmpp tem
   `QXmppCallManager`, mas a parte de áudio e vídeo (GStreamer) ainda precisa
   ser ligada a ele.
3. **OMEMO** (XEP-0384), por último: a equipe ainda não usa. O QXmpp tem
   `QXmppOmemoManager`, que depende do QCA e de guardar as chaves.

## A implementar

Pedidos para o uso do dia a dia, sem ordem definida:

- Opção de a janela ganhar foco ao receber uma mensagem, no menu de preferências
- Clicar na notificação abre o Omajim, já na conversa da mensagem recebida 
  (parece já estar funcionando)
- Corrigir uma mensagem formatada mantendo a formatação (hoje a correção
  volta ao texto com os marcadores do Gajim)

## Ainda não portado do Gajim

Coisas que o Gajim tem e o Omajim (Qt) não. Não sabemos se a equipe usa cada
uma; confirmar antes de portar.

- Moderação de salas: expulsar, banir, mudar o papel e a afiliação dos
  participantes, e configurar a sala (o app só avisa quando você é banido)
- Convidar contatos para uma sala (o app já aceita convites recebidos)
- Editar o próprio perfil: avatar, nome e vCard
- Trocar a senha da conta
- Bloquear contatos (XEP-0191)
- Apagar e moderar mensagens (XEP-0424/0425)
- Corretor ortográfico no campo de mensagem
- Comandos ad-hoc (XEP-0050) e descoberta de serviços
- Criar uma conta nova no servidor (XEP-0077)

## Em aberto

- **Testar com uma conta real.** Até agora tudo foi conferido nos testes e
  numa tela de teste com dados inventados.
- **Recorte de cartões de arquivo.** Na tela de teste, um cartão de arquivo no
  topo da lista apareceu cortado. Ver se isso acontece na tela de verdade.
