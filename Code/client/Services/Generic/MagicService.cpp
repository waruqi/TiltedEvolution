#include <Services/MagicService.h>

#include <World.h>

#include <Events/SpellCastEvent.h>
#include <Events/InterruptCastEvent.h>
#include <Events/AddTargetEvent.h>

#include <Messages/SpellCastRequest.h>
#include <Messages/InterruptCastRequest.h>
#include <Messages/AddTargetRequest.h>

#include <Messages/NotifySpellCast.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyAddTarget.h>

#include <Actor.h>

#if TP_SKYRIM64
#include <Misc/ActorMagicCaster.h>
#endif

MagicService::MagicService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept 
    : m_world(aWorld), m_dispatcher(aDispatcher), m_transport(aTransport)
{
    m_spellCastEventConnection = m_dispatcher.sink<SpellCastEvent>().connect<&MagicService::OnSpellCastEvent>(this);
    m_notifySpellCastConnection = m_dispatcher.sink<NotifySpellCast>().connect<&MagicService::OnNotifySpellCast>(this);
    m_interruptCastEventConnection = m_dispatcher.sink<InterruptCastEvent>().connect<&MagicService::OnInterruptCastEvent>(this);
    m_notifyInterruptCastConnection = m_dispatcher.sink<NotifyInterruptCast>().connect<&MagicService::OnNotifyInterruptCast>(this);
    m_addTargetEventConnection = m_dispatcher.sink<AddTargetEvent>().connect<&MagicService::OnAddTargetEvent>(this);
    m_notifyAddTargetConnection = m_dispatcher.sink<NotifyAddTarget>().connect<&MagicService::OnNotifyAddTarget>(this);
}

void MagicService::OnSpellCastEvent(const SpellCastEvent& acSpellCastEvent) const noexcept
{
#if TP_SKYRIM64
    TP_ASSERT(acSpellCastEvent.pSpell, "SpellCastEvent has no spell");

    if (!acSpellCastEvent.pCaster->pCasterActor || !acSpellCastEvent.pCaster->pCasterActor->GetNiNode())
    {
        spdlog::warn("Spell cast event has no actor or actor is not loaded");
        return;
    }

    uint32_t formId = acSpellCastEvent.pCaster->pCasterActor->formID;

    auto view = m_world.view<FormIdComponent, LocalComponent>();
    const auto casterEntityIt = std::find_if(std::begin(view), std::end(view), [formId, view](entt::entity entity)
    {
        return view.get<FormIdComponent>(entity).Id == formId;
    });

    if (casterEntityIt == std::end(view))
        return;

    auto& localComponent = view.get<LocalComponent>(*casterEntityIt);

    SpellCastRequest request;
    request.CasterId = localComponent.Id;
    request.CastingSource = acSpellCastEvent.pCaster->GetCastingSource();
    request.IsDualCasting = acSpellCastEvent.pCaster->GetIsDualCasting();
    m_world.GetModSystem().GetServerModId(acSpellCastEvent.pSpell->formID, request.SpellFormId.ModId,
                                          request.SpellFormId.BaseId);

    spdlog::info("Spell cast event sent, ID: {:X}, Source: {}, IsDualCasting: {}", request.CasterId,
                 request.CastingSource, request.IsDualCasting);

    m_transport.Send(request);
#endif
}

void MagicService::OnNotifySpellCast(const NotifySpellCast& acMessage) const noexcept
{
#if TP_SKYRIM64
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.CasterId](auto entity)
    {
        return remoteView.get<RemoteComponent>(entity).Id == Id;
    });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Caster with remote id {:X} not found.", acMessage.CasterId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);

    auto* pForm = TESForm::GetById(formIdComponent.Id);
    auto* pActor = RTTI_CAST(pForm, TESForm, Actor);

    if (!pActor->leftHandCaster)
        pActor->leftHandCaster = (ActorMagicCaster*)pActor->GetMagicCaster(MagicSystem::CastingSource::LEFT_HAND);
    if (!pActor->rightHandCaster)
        pActor->rightHandCaster = (ActorMagicCaster*)pActor->GetMagicCaster(MagicSystem::CastingSource::RIGHT_HAND);
    if (!pActor->shoutCaster)
        pActor->shoutCaster = (ActorMagicCaster*)pActor->GetMagicCaster(MagicSystem::CastingSource::OTHER);

    // Only left hand casters need dual casting (?)
    pActor->leftHandCaster->SetDualCasting(acMessage.IsDualCasting);

    MagicItem* pSpell = nullptr;

    if (acMessage.CastingSource >= 4)
    {
        spdlog::warn("Casting source out of bounds, trying form id");
    }
    else
    {
        pSpell = pActor->magicItems[acMessage.CastingSource];
    }

    if (!pSpell)
    {
        const uint32_t cSpellFormId = World::Get().GetModSystem().GetGameId(acMessage.SpellFormId);
        if (cSpellFormId == 0)
        {
            spdlog::error("Could not find spell form id for GameId base {:X}, mod {:X}", acMessage.SpellFormId.BaseId,
                          acMessage.SpellFormId.ModId);
            return;
        }
        auto* pSpellForm = TESForm::GetById(cSpellFormId);
        if (!pSpellForm)
        {
            spdlog::error("Cannot find spell form");
        }
        else
            pSpell = RTTI_CAST(pSpellForm, TESForm, MagicItem);
    }

    switch (acMessage.CastingSource)
    {
    case MagicSystem::CastingSource::LEFT_HAND:
        pActor->leftHandCaster->CastSpellImmediate(pSpell, false, nullptr, 1.0f, false, 0.0f);
        break;
    case MagicSystem::CastingSource::RIGHT_HAND:
        pActor->rightHandCaster->CastSpellImmediate(pSpell, false, nullptr, 1.0f, false, 0.0f);
        break;
    case MagicSystem::CastingSource::OTHER:
        pActor->shoutCaster->CastSpellImmediate(pSpell, false, nullptr, 1.0f, false, 0.0f);
        break;
    case MagicSystem::CastingSource::INSTANT:
        break;
    }
#endif
}

void MagicService::OnInterruptCastEvent(const InterruptCastEvent& acEvent) const noexcept
{
#if TP_SKYRIM64
    auto formId = acEvent.CasterFormID;

    auto view = m_world.view<FormIdComponent, LocalComponent>();
    const auto casterEntityIt = std::find_if(std::begin(view), std::end(view), [formId, view](entt::entity entity)
    {
        return view.get<FormIdComponent>(entity).Id == formId;
    });

    if (casterEntityIt == std::end(view))
        return;

    auto& localComponent = view.get<LocalComponent>(*casterEntityIt);

    InterruptCastRequest request;
    request.CasterId = localComponent.Id;
    m_transport.Send(request);
#endif
}

void MagicService::OnNotifyInterruptCast(const NotifyInterruptCast& acMessage) const noexcept
{
#if TP_SKYRIM64
    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.CasterId](auto entity)
    {
        return remoteView.get<RemoteComponent>(entity).Id == Id;
    });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Caster with remote id {:X} not found.", acMessage.CasterId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);

    auto* pForm = TESForm::GetById(formIdComponent.Id);
    auto* pActor = RTTI_CAST(pForm, TESForm, Actor);

    pActor->InterruptCast(false);

    spdlog::info("Interrupt remote cast successful");
#endif
}

void MagicService::OnAddTargetEvent(const AddTargetEvent& acEvent) const noexcept
{
#if TP_SKYRIM64
    auto view = m_world.view<FormIdComponent>();
    for (auto entity : view)
    {
        auto& formIdComponent = view.get<FormIdComponent>(entity);
        if (formIdComponent.Id != acEvent.TargetID)
            continue;

        AddTargetRequest request;

        if (const auto* pLocalComponent = m_world.try_get<LocalComponent>(entity))
        {
            request.TargetId = pLocalComponent->Id;
        }
        else if (const auto* pRemoteComponent = m_world.try_get<RemoteComponent>(entity))
        {
            request.TargetId = pRemoteComponent->Id;
        }

        TP_ASSERT(request.TargetId, "AddTargetRequest must have a target id.");

        if (!m_world.GetModSystem().GetServerModId(acEvent.SpellID, request.SpellId.ModId, request.SpellId.BaseId))
        {
            spdlog::error("{s}: Could not find spell with from {:X}", __FUNCTION__, acEvent.SpellID);
            return;
        }

        m_transport.Send(request);

        break;
    }
#endif
}

void MagicService::OnNotifyAddTarget(const NotifyAddTarget& acMessage) const noexcept
{
#if TP_SKYRIM64
    auto view = m_world.view<FormIdComponent>();

    for (auto entity : view)
    {
        uint32_t componentId;
        const auto cpLocalComponent = m_world.try_get<LocalComponent>(entity);
        const auto cpRemoteComponent = m_world.try_get<RemoteComponent>(entity);

        if (cpLocalComponent)
            componentId = cpLocalComponent->Id;
        else if (cpRemoteComponent)
            componentId = cpRemoteComponent->Id;
        else
            continue;

        if (componentId == acMessage.TargetId)
        {
            auto& formIdComponent = view.get<FormIdComponent>(entity);
            auto* pForm = TESForm::GetById(formIdComponent.Id);
            auto* pActor = RTTI_CAST(pForm, TESForm, Actor);

            TP_ASSERT(pActor, "Actor should exist, form id: {:X}", formIdComponent.Id);

            if (pActor)
            {
                const auto cSpellId = World::Get().GetModSystem().GetGameId(acMessage.SpellId);
                if (cSpellId == 0)
                {
                    spdlog::error("{}: failed to retrieve spell id, GameId base: {:X}, mod: {:X}", __FUNCTION__,
                                  acMessage.SpellId.BaseId, acMessage.SpellId.ModId);
                    return;
                }

                auto* pSpell = static_cast<MagicItem*>(TESForm::GetById(cSpellId));
                if (!pSpell)
                {
                    spdlog::error("{}: Failed to retrieve spell by id {:X}", cSpellId);
                    return;
                }

                // TODO: AddTarget gets notified for every effect, but we loop through the effects here again
                for (auto effect : pSpell->listOfEffects)
                {
                    MagicTarget::AddTargetData data{};
                    data.pSpell = pSpell;
                    data.pEffectItem = effect;
                    data.fMagnitude = 0.0f;
                    data.fUnkFloat1 = 1.0f;
                    data.eCastingSource = MagicSystem::CastingSource::CASTING_SOURCE_COUNT;

                    pActor->magicTarget.AddTarget(data);
                }

                break;
            }
        }
    }
#endif
}