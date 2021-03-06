using CryEngine.Common;

using CryEngine.EntitySystem;

namespace CryEngine
{
	/// <summary>
	/// Wrapper class for a ray cast output.
	/// </summary>
	public class RaycastHit
	{
		public enum HitType
		{
			Unknown,
			Terrain,
			Entity,
			Static,
		}

		private uint _hitID;

		public ray_hit NativeHandle { get; private set; }
		public int Hits { get; private set; }
		public bool Intersected { get { return Hits != 0; } }
		public Vector3 Point { get { return NativeHandle.pt; } }
		public Vector3 Normal { get { return NativeHandle.n; } }
		public float Distance { get { return NativeHandle.dist; } }

		/// <summary>
		/// The native CRYENGINE entity that was hit. Returns null if nothing was hit, or the hit wasn't an Entity.
		/// </summary>
		public IEntity HitNativeEntity { get { return _hitID != 0 ? Global.gEnv.pEntitySystem.GetEntity(_hitID) : null; } }

		/// <summary>
		/// The CESharp entity that was hit. Returns null if nothing was hit, the entity isn't a CESharp entity, or the hit wasn't an Entity.
		/// </summary>
		public Entity HitBaseEntity { get { return Entity.Get(_hitID); } }

		/// <summary>
		/// The type of surface that was hit, if any. Returns 'Unknown' if nothing was hit.
		/// </summary>
		public HitType Type { get; private set; }

		/// <summary>
		/// The native entity's physical entity, if something was hit.
		/// </summary>
		public IPhysicalEntity Collider { get { return NativeHandle.pCollider; } }

		public RaycastHit(ray_hit ray, int hits)
		{
			NativeHandle = ray;
			Hits = hits;

			if (ray.bTerrain == 1)
			{
				Type = HitType.Terrain;
			}
			else
			{
				var pEntity = Global.gEnv.pEntitySystem.GetEntityFromPhysics(ray.pCollider);

				if (pEntity != null)
					pEntity = pEntity.GetAttachedChild(ray.partid);

				if (pEntity != null)
				{
					// We hit some sort of entity.
					_hitID = pEntity.GetId();

					Type = HitType.Entity;
				}
				else
				{
					// We hit something that isn't registered with the entity system. Probably a designer object or brush.
					Type = HitType.Static;
				}
			}
		}

		/// <summary>
		/// Returns True if the entity passed in was hit.
		public bool HasHitEntity(Entity entity)
		{
			return HitBaseEntity != null ? HitBaseEntity.Id == entity.Id : false;
		}
	}
}
